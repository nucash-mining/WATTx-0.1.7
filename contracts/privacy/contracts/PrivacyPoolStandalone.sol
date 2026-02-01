// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "@openzeppelin/contracts/token/ERC20/IERC20.sol";
import "@openzeppelin/contracts/token/ERC20/utils/SafeERC20.sol";
import "@openzeppelin/contracts/security/ReentrancyGuard.sol";
import "@openzeppelin/contracts/access/Ownable.sol";

/**
 * @title PrivacyPoolStandalone
 * @notice Standalone privacy pool for single-chain anonymous transfers
 * @dev Simplified version without LayerZero - can be upgraded later
 *
 * This version allows:
 * - Deposit USDT with a commitment (shielded)
 * - Withdraw with ZK proof to any address (anonymous)
 * - Same chain only - cross-chain requires LayerZero upgrade
 */
contract PrivacyPoolStandalone is ReentrancyGuard, Ownable {
    using SafeERC20 for IERC20;

    // ============ Constants ============

    // Fixed denominations for anonymity (amounts in 6 decimals for USDT)
    uint256 public constant DENOM_100 = 100 * 1e6;      // 100 USDT
    uint256 public constant DENOM_1000 = 1000 * 1e6;    // 1,000 USDT
    uint256 public constant DENOM_10000 = 10000 * 1e6;  // 10,000 USDT
    uint256 public constant DENOM_100000 = 100000 * 1e6; // 100,000 USDT

    uint256 public constant MERKLE_TREE_LEVELS = 20;

    // ============ State Variables ============

    IERC20 public immutable token;  // USDT or other stablecoin
    address public verifier;         // ZK proof verifier contract

    // Merkle tree state
    uint256 public nextIndex;
    bytes32[] public zeros;
    bytes32[] public filledSubtrees;
    bytes32[] public roots;
    mapping(bytes32 => bool) public rootHistory;
    uint256 public constant ROOT_HISTORY_SIZE = 100;

    // Commitment and nullifier tracking
    mapping(bytes32 => bool) public commitments;
    mapping(bytes32 => bool) public nullifiers;

    // Statistics
    uint256 public totalDeposited;
    uint256 public totalWithdrawn;

    // Fee configuration
    uint256 public depositFeeBps = 10;    // 0.1%
    uint256 public withdrawFeeBps = 10;   // 0.1%
    address public feeRecipient;

    // ============ Events ============

    event Deposit(
        bytes32 indexed commitment,
        uint256 indexed leafIndex,
        uint256 amount,
        uint256 timestamp
    );

    event Withdrawal(
        bytes32 indexed nullifier,
        address indexed recipient,
        uint256 amount,
        uint256 timestamp
    );

    event MerkleRoot(bytes32 indexed root, uint256 leafCount);

    // ============ Errors ============

    error InvalidDenomination();
    error InvalidProof();
    error NullifierAlreadyUsed();
    error CommitmentAlreadyExists();
    error InvalidRoot();
    error TreeFull();
    error ZeroAddress();

    // ============ Constructor ============

    constructor(
        address _token,
        address _verifier,
        address _owner
    ) {
        if (_token == address(0)) revert ZeroAddress();
        token = IERC20(_token);
        verifier = _verifier;
        feeRecipient = _owner;
        _transferOwnership(_owner);

        // Initialize Merkle tree
        _initMerkleTree();
    }

    // ============ Merkle Tree Functions ============

    function _initMerkleTree() internal {
        bytes32 currentZero = bytes32(0);
        zeros.push(currentZero);
        filledSubtrees.push(currentZero);

        for (uint256 i = 1; i < MERKLE_TREE_LEVELS; i++) {
            currentZero = _hashPair(currentZero, currentZero);
            zeros.push(currentZero);
            filledSubtrees.push(currentZero);
        }

        bytes32 emptyRoot = _hashPair(currentZero, currentZero);
        roots.push(emptyRoot);
        rootHistory[emptyRoot] = true;
    }

    function _insert(bytes32 leaf) internal returns (uint256 index) {
        uint256 _nextIndex = nextIndex;
        if (_nextIndex >= 2**MERKLE_TREE_LEVELS) revert TreeFull();

        index = _nextIndex;
        bytes32 currentHash = leaf;
        uint256 currentIndex = _nextIndex;

        for (uint256 i = 0; i < MERKLE_TREE_LEVELS; i++) {
            if (currentIndex % 2 == 0) {
                filledSubtrees[i] = currentHash;
                currentHash = _hashPair(currentHash, zeros[i]);
            } else {
                currentHash = _hashPair(filledSubtrees[i], currentHash);
            }
            currentIndex = currentIndex / 2;
        }

        roots.push(currentHash);
        rootHistory[currentHash] = true;

        if (roots.length > ROOT_HISTORY_SIZE) {
            bytes32 oldRoot = roots[roots.length - ROOT_HISTORY_SIZE - 1];
            rootHistory[oldRoot] = false;
        }

        nextIndex = _nextIndex + 1;
        emit MerkleRoot(currentHash, nextIndex);
    }

    function _hashPair(bytes32 left, bytes32 right) internal pure returns (bytes32) {
        return keccak256(abi.encodePacked(left, right));
    }

    // ============ Deposit Function ============

    /**
     * @notice Deposit tokens with a commitment
     * @param commitment The commitment hash (computed off-chain)
     * @param amount Amount to deposit (must be valid denomination)
     */
    function deposit(bytes32 commitment, uint256 amount) external nonReentrant {
        _validateDenomination(amount);
        if (commitments[commitment]) revert CommitmentAlreadyExists();

        // Calculate fee
        uint256 fee = (amount * depositFeeBps) / 10000;
        uint256 netAmount = amount - fee;

        // Transfer tokens
        token.safeTransferFrom(msg.sender, address(this), amount);

        // Collect fee
        if (fee > 0) {
            token.safeTransfer(feeRecipient, fee);
        }

        // Add commitment to tree
        uint256 leafIndex = _insert(commitment);
        commitments[commitment] = true;
        totalDeposited += netAmount;

        emit Deposit(commitment, leafIndex, netAmount, block.timestamp);
    }

    // ============ Withdraw Function ============

    /**
     * @notice Withdraw tokens with ZK proof
     * @param proof ZK proof data (8 uint256 for Groth16)
     * @param root Merkle root to verify against
     * @param nullifier Unique nullifier to prevent double-spend
     * @param recipient Address to receive tokens
     * @param amount Amount to withdraw
     */
    function withdraw(
        bytes calldata proof,
        bytes32 root,
        bytes32 nullifier,
        address recipient,
        uint256 amount
    ) external nonReentrant {
        if (nullifiers[nullifier]) revert NullifierAlreadyUsed();
        if (!rootHistory[root]) revert InvalidRoot();
        if (recipient == address(0)) revert ZeroAddress();

        // Verify ZK proof
        if (verifier != address(0)) {
            uint256[8] memory proofData = abi.decode(proof, (uint256[8]));
            uint256[4] memory publicInputs = [
                uint256(root),
                uint256(nullifier),
                amount,
                uint256(uint160(recipient))
            ];

            (bool success, bytes memory result) = verifier.staticcall(
                abi.encodeWithSignature(
                    "verifyProof(uint256[8],uint256[4])",
                    proofData,
                    publicInputs
                )
            );

            if (!success || (result.length > 0 && !abi.decode(result, (bool)))) {
                revert InvalidProof();
            }
        }

        // Mark nullifier as used
        nullifiers[nullifier] = true;

        // Calculate fee
        uint256 fee = (amount * withdrawFeeBps) / 10000;
        uint256 netAmount = amount - fee;

        // Update stats
        totalWithdrawn += amount;

        // Transfer tokens
        token.safeTransfer(recipient, netAmount);

        // Collect fee
        if (fee > 0) {
            token.safeTransfer(feeRecipient, fee);
        }

        emit Withdrawal(nullifier, recipient, netAmount, block.timestamp);
    }

    // ============ View Functions ============

    function getRoot() public view returns (bytes32) {
        return roots[roots.length - 1];
    }

    function isKnownRoot(bytes32 root) public view returns (bool) {
        return rootHistory[root];
    }

    function isValidDenomination(uint256 amount) public pure returns (bool) {
        return amount == DENOM_100 ||
               amount == DENOM_1000 ||
               amount == DENOM_10000 ||
               amount == DENOM_100000;
    }

    function getStats() external view returns (
        uint256 deposited,
        uint256 withdrawn,
        uint256 commitmentCount,
        bytes32 currentRoot
    ) {
        return (totalDeposited, totalWithdrawn, nextIndex, getRoot());
    }

    // ============ Admin Functions ============

    function setVerifier(address _verifier) external onlyOwner {
        verifier = _verifier;
    }

    function setFees(uint256 _depositFeeBps, uint256 _withdrawFeeBps) external onlyOwner {
        require(_depositFeeBps <= 100 && _withdrawFeeBps <= 100, "Fee too high");
        depositFeeBps = _depositFeeBps;
        withdrawFeeBps = _withdrawFeeBps;
    }

    function setFeeRecipient(address _feeRecipient) external onlyOwner {
        if (_feeRecipient == address(0)) revert ZeroAddress();
        feeRecipient = _feeRecipient;
    }

    // ============ Internal Functions ============

    function _validateDenomination(uint256 amount) internal pure {
        if (!isValidDenomination(amount)) revert InvalidDenomination();
    }
}
