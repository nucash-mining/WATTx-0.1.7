// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "@openzeppelin/contracts/security/ReentrancyGuard.sol";
import "@openzeppelin/contracts/access/Ownable.sol";
import "@layerzerolabs/lz-evm-oapp-v2/contracts/oapp/OApp.sol";
import "./MerkleTree.sol";
import "./IVerifier.sol";

/**
 * @title PrivacyController
 * @notice Central controller for cross-chain private transfers on WATTx
 * @dev Manages shielded commitments and withdrawal proofs
 *
 * This contract:
 * 1. Receives deposit notifications from external chain pools via LayerZero
 * 2. Maintains a Merkle tree of commitments (shielded balances)
 * 3. Verifies ZK proofs for withdrawals
 * 4. Sends withdrawal requests to destination chain pools
 */
contract PrivacyController is OApp, ReentrancyGuard, MerkleTree {

    // ============ Constants ============

    uint8 public constant MSG_DEPOSIT = 1;
    uint8 public constant MSG_WITHDRAW = 2;
    uint8 public constant MSG_REBALANCE = 3;

    uint256 public constant MERKLE_TREE_LEVELS = 20;  // Supports 2^20 = ~1M commitments

    // ============ State Variables ============

    IVerifier public immutable withdrawVerifier;

    // Commitment tracking
    mapping(bytes32 => bool) public commitments;
    mapping(bytes32 => bool) public nullifiers;
    uint256 public totalCommitments;

    // Chain configuration
    mapping(uint32 => bool) public supportedChains;      // LayerZero chain IDs
    mapping(uint32 => address) public chainPools;        // Pool addresses on each chain
    mapping(uint32 => uint256) public chainLiquidity;    // Tracked liquidity per chain

    // Statistics
    uint256 public totalDeposited;
    uint256 public totalWithdrawn;
    mapping(uint32 => uint256) public depositsPerChain;
    mapping(uint32 => uint256) public withdrawalsPerChain;

    // ============ Events ============

    event CommitmentAdded(
        bytes32 indexed commitment,
        uint32 indexed sourceChain,
        uint256 leafIndex,
        uint256 timestamp
    );

    event WithdrawalInitiated(
        bytes32 indexed nullifier,
        uint32 indexed destChain,
        address indexed recipient,
        uint256 amount,
        uint256 timestamp
    );

    event ChainConfigured(uint32 indexed chainId, address poolAddress);
    event NewMerkleRoot(bytes32 indexed root, uint256 totalLeaves);

    // ============ Errors ============

    error InvalidProof();
    error NullifierAlreadyUsed();
    error CommitmentAlreadyExists();
    error UnsupportedChain();
    error InsufficientChainLiquidity();
    error InvalidCommitment();
    error ZeroAddress();

    // ============ Constructor ============

    constructor(
        address _lzEndpoint,
        address _verifier,
        address _owner
    ) OApp(_lzEndpoint, _owner) Ownable(_owner) MerkleTree(MERKLE_TREE_LEVELS) {
        if (_verifier == address(0)) revert ZeroAddress();
        withdrawVerifier = IVerifier(_verifier);
    }

    // ============ LayerZero Receive (Deposits from external chains) ============

    function _lzReceive(
        Origin calldata _origin,
        bytes32 /*_guid*/,
        bytes calldata _payload,
        address /*_executor*/,
        bytes calldata /*_extraData*/
    ) internal override {
        uint32 srcChainId = _origin.srcEid;
        if (!supportedChains[srcChainId]) revert UnsupportedChain();

        (uint8 msgType, bytes memory data) = abi.decode(_payload, (uint8, bytes));

        if (msgType == MSG_DEPOSIT) {
            _processDeposit(srcChainId, data);
        }
    }

    /**
     * @notice Process incoming deposit from external chain
     */
    function _processDeposit(uint32 srcChainId, bytes memory data) internal {
        (
            uint256 amount,
            bytes32 stealthPubKeyX,
            bytes32 stealthPubKeyY,
            bytes32 depositHash
        ) = abi.decode(data, (uint256, bytes32, bytes32, bytes32));

        // Create commitment: Hash(amount, stealthPubKey, randomness)
        // The actual commitment is computed by the user off-chain
        // Here we use depositHash as a placeholder - in production,
        // user provides the commitment and we verify it matches the deposit
        bytes32 commitment = keccak256(abi.encodePacked(
            amount,
            stealthPubKeyX,
            stealthPubKeyY,
            depositHash
        ));

        if (commitments[commitment]) revert CommitmentAlreadyExists();

        // Add to Merkle tree
        uint256 leafIndex = _insert(commitment);
        commitments[commitment] = true;
        totalCommitments++;

        // Update statistics
        totalDeposited += amount;
        depositsPerChain[srcChainId] += amount;
        chainLiquidity[srcChainId] += amount;

        emit CommitmentAdded(commitment, srcChainId, leafIndex, block.timestamp);
        emit NewMerkleRoot(getRoot(), totalCommitments);

        // Emit event for UTXO layer to create private output
        // This will be picked up by WATTx node to mint shielded coins
        _emitShieldedMint(amount, stealthPubKeyX, stealthPubKeyY);
    }

    // ============ Withdrawal Functions ============

    /**
     * @notice Withdraw shielded funds to any supported chain
     * @param proof ZK proof proving ownership of commitment
     * @param root Merkle root the proof is against
     * @param nullifier Unique nullifier to prevent double-spend
     * @param amount Amount to withdraw
     * @param destChainId LayerZero chain ID to withdraw to
     * @param recipient Address on destination chain
     */
    function withdraw(
        bytes calldata proof,
        bytes32 root,
        bytes32 nullifier,
        uint256 amount,
        uint32 destChainId,
        address recipient
    ) external payable nonReentrant {
        // Validate inputs
        if (nullifiers[nullifier]) revert NullifierAlreadyUsed();
        if (!supportedChains[destChainId]) revert UnsupportedChain();
        if (!isKnownRoot(root)) revert InvalidProof();
        if (chainLiquidity[destChainId] < amount) revert InsufficientChainLiquidity();

        // Verify ZK proof
        // The proof demonstrates:
        // 1. Prover knows a commitment in the Merkle tree
        // 2. Prover knows the secret (amount, secret key) that created the commitment
        // 3. The nullifier is correctly derived from the secret
        // WITHOUT revealing which commitment or the actual secret
        uint256[8] memory proofData = abi.decode(proof, (uint256[8]));
        uint256[4] memory publicInputs = [
            uint256(root),
            uint256(nullifier),
            amount,
            uint256(uint160(recipient))
        ];

        if (!withdrawVerifier.verifyProof(proofData, publicInputs)) {
            revert InvalidProof();
        }

        // Mark nullifier as used
        nullifiers[nullifier] = true;

        // Update statistics
        totalWithdrawn += amount;
        withdrawalsPerChain[destChainId] += amount;
        chainLiquidity[destChainId] -= amount;

        // Send withdrawal message to destination chain pool
        bytes memory payload = abi.encode(
            MSG_WITHDRAW,
            abi.encode(nullifier, recipient, amount)
        );

        _lzSend(
            destChainId,
            payload,
            _buildOptions(200000),
            MessagingFee(msg.value, 0),
            payable(msg.sender)
        );

        emit WithdrawalInitiated(nullifier, destChainId, recipient, amount, block.timestamp);
    }

    /**
     * @notice Get quote for withdrawal to specific chain
     */
    function quoteWithdraw(uint32 destChainId, uint256 amount) external view returns (
        uint256 lzFee,
        bool hasLiquidity
    ) {
        hasLiquidity = chainLiquidity[destChainId] >= amount;
        // LZ fee estimation
        lzFee = 0; // Placeholder - implement proper estimation
    }

    // ============ Internal Functions ============

    function _emitShieldedMint(
        uint256 amount,
        bytes32 stealthPubKeyX,
        bytes32 stealthPubKeyY
    ) internal {
        // This event is picked up by WATTx node to create UTXO-side shielded output
        emit ShieldedMint(amount, stealthPubKeyX, stealthPubKeyY);
    }

    event ShieldedMint(uint256 amount, bytes32 stealthPubKeyX, bytes32 stealthPubKeyY);

    function _buildOptions(uint128 gasLimit) internal pure returns (bytes memory) {
        return abi.encodePacked(uint16(1), gasLimit);
    }

    // ============ Admin Functions ============

    /**
     * @notice Configure a supported chain and its pool address
     */
    function configureChain(
        uint32 chainId,
        address poolAddress,
        uint256 initialLiquidity
    ) external onlyOwner {
        if (poolAddress == address(0)) revert ZeroAddress();
        supportedChains[chainId] = true;
        chainPools[chainId] = poolAddress;
        chainLiquidity[chainId] = initialLiquidity;
        emit ChainConfigured(chainId, poolAddress);
    }

    /**
     * @notice Remove chain support
     */
    function removeChain(uint32 chainId) external onlyOwner {
        supportedChains[chainId] = false;
        chainPools[chainId] = address(0);
    }

    /**
     * @notice Update liquidity tracking for a chain
     */
    function updateChainLiquidity(uint32 chainId, uint256 liquidity) external onlyOwner {
        chainLiquidity[chainId] = liquidity;
    }

    // ============ View Functions ============

    function getStats() external view returns (
        uint256 _totalCommitments,
        uint256 _totalDeposited,
        uint256 _totalWithdrawn,
        bytes32 _currentRoot
    ) {
        return (totalCommitments, totalDeposited, totalWithdrawn, getRoot());
    }

    function getChainStats(uint32 chainId) external view returns (
        bool supported,
        address pool,
        uint256 liquidity,
        uint256 deposits,
        uint256 withdrawals
    ) {
        return (
            supportedChains[chainId],
            chainPools[chainId],
            chainLiquidity[chainId],
            depositsPerChain[chainId],
            withdrawalsPerChain[chainId]
        );
    }

    function isNullifierUsed(bytes32 nullifier) external view returns (bool) {
        return nullifiers[nullifier];
    }

    function isCommitmentKnown(bytes32 commitment) external view returns (bool) {
        return commitments[commitment];
    }
}
