// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "@openzeppelin/contracts/token/ERC20/IERC20.sol";
import "@openzeppelin/contracts/token/ERC20/utils/SafeERC20.sol";
import "@openzeppelin/contracts/security/ReentrancyGuard.sol";
import "@openzeppelin/contracts/access/Ownable.sol";

/**
 * @title PrivacyPoolBridge
 * @notice Cross-chain privacy pool for anonymous bridging
 * @dev Supports Merkle root sync from other chains for cross-chain withdrawals
 *
 * Flow:
 * 1. User deposits on Chain A (e.g., Sepolia) - gets commitment added to Merkle tree
 * 2. Relayer syncs Merkle root from Chain A to Chain B (e.g., Altcoinchain)
 * 3. User withdraws on Chain B with ZK proof - proves knowledge of commitment without revealing which one
 */
contract PrivacyPoolBridge is ReentrancyGuard, Ownable {
    using SafeERC20 for IERC20;

    // ============ Constants ============

    uint256 public constant MERKLE_TREE_LEVELS = 20;
    uint256 public constant ROOT_HISTORY_SIZE = 100;

    // ============ State Variables ============

    IERC20 public immutable token;
    address public verifier;

    // Local Merkle tree
    uint256 public nextIndex;
    bytes32[] public zeros;
    bytes32[] public filledSubtrees;
    bytes32[] public roots;
    mapping(bytes32 => bool) public localRootHistory;

    // Cross-chain roots from other chains
    struct ExternalRoot {
        bytes32 root;
        uint256 sourceChainId;
        uint256 timestamp;
        bool active;
    }
    mapping(bytes32 => ExternalRoot) public externalRoots;
    bytes32[] public externalRootList;

    // Authorized relayers for cross-chain root sync
    mapping(address => bool) public relayers;

    // Nullifier tracking (global across all chains)
    mapping(bytes32 => bool) public nullifiers;

    // Commitments (local only)
    mapping(bytes32 => bool) public commitments;

    // Stats
    uint256 public totalDeposited;
    uint256 public totalWithdrawn;
    uint256 public totalBridgedIn;  // Withdrawals from external roots

    // Fees
    uint256 public depositFeeBps = 10;
    uint256 public withdrawFeeBps = 10;
    uint256 public bridgeFeeBps = 30;  // Higher fee for cross-chain (0.3%)
    address public feeRecipient;

    // ============ Events ============

    event Deposit(bytes32 indexed commitment, uint256 indexed leafIndex, uint256 amount, uint256 timestamp);
    event Withdrawal(bytes32 indexed nullifier, address indexed recipient, uint256 amount, uint256 sourceChainId);
    event MerkleRoot(bytes32 indexed root, uint256 leafCount);
    event ExternalRootAdded(bytes32 indexed root, uint256 indexed sourceChainId, address relayer);
    event ExternalRootRevoked(bytes32 indexed root);
    event RelayerUpdated(address indexed relayer, bool authorized);

    // ============ Errors ============

    error InvalidProof();
    error NullifierAlreadyUsed();
    error CommitmentAlreadyExists();
    error InvalidRoot();
    error TreeFull();
    error ZeroAddress();
    error NotRelayer();
    error RootAlreadyExists();
    error InsufficientLiquidity();

    // ============ Modifiers ============

    modifier onlyRelayer() {
        if (!relayers[msg.sender] && msg.sender != owner()) revert NotRelayer();
        _;
    }

    // ============ Constructor ============

    constructor(address _token, address _verifier, address _owner) {
        if (_token == address(0)) revert ZeroAddress();
        token = IERC20(_token);
        verifier = _verifier;
        feeRecipient = _owner;
        _transferOwnership(_owner);
        _initMerkleTree();

        // Owner is default relayer
        relayers[_owner] = true;
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
        localRootHistory[emptyRoot] = true;
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
        localRootHistory[currentHash] = true;

        if (roots.length > ROOT_HISTORY_SIZE) {
            bytes32 oldRoot = roots[roots.length - ROOT_HISTORY_SIZE - 1];
            localRootHistory[oldRoot] = false;
        }

        nextIndex = _nextIndex + 1;
        emit MerkleRoot(currentHash, nextIndex);
    }

    function _hashPair(bytes32 left, bytes32 right) internal pure returns (bytes32) {
        return keccak256(abi.encodePacked(left, right));
    }

    // ============ Cross-Chain Root Sync ============

    /**
     * @notice Add a Merkle root from another chain (relayer only)
     * @param root The Merkle root from source chain
     * @param sourceChainId The chain ID where the deposit was made
     */
    function addExternalRoot(bytes32 root, uint256 sourceChainId) external onlyRelayer {
        if (externalRoots[root].active) revert RootAlreadyExists();

        externalRoots[root] = ExternalRoot({
            root: root,
            sourceChainId: sourceChainId,
            timestamp: block.timestamp,
            active: true
        });
        externalRootList.push(root);

        emit ExternalRootAdded(root, sourceChainId, msg.sender);
    }

    /**
     * @notice Revoke an external root (emergency only)
     */
    function revokeExternalRoot(bytes32 root) external onlyOwner {
        externalRoots[root].active = false;
        emit ExternalRootRevoked(root);
    }

    /**
     * @notice Check if a root is valid (local or external)
     */
    function isValidRoot(bytes32 root) public view returns (bool isValid, uint256 sourceChainId) {
        if (localRootHistory[root]) {
            return (true, block.chainid);
        }
        if (externalRoots[root].active) {
            return (true, externalRoots[root].sourceChainId);
        }
        return (false, 0);
    }

    // ============ Deposit Function ============

    function deposit(bytes32 commitment, uint256 amount) external nonReentrant {
        if (commitments[commitment]) revert CommitmentAlreadyExists();

        uint256 fee = (amount * depositFeeBps) / 10000;
        uint256 netAmount = amount - fee;

        token.safeTransferFrom(msg.sender, address(this), amount);

        if (fee > 0) {
            token.safeTransfer(feeRecipient, fee);
        }

        uint256 leafIndex = _insert(commitment);
        commitments[commitment] = true;
        totalDeposited += netAmount;

        emit Deposit(commitment, leafIndex, netAmount, block.timestamp);
    }

    // ============ Withdraw Function (supports cross-chain) ============

    /**
     * @notice Withdraw tokens with ZK proof
     * @dev Works with both local and external (cross-chain) roots
     */
    function withdraw(
        bytes calldata proof,
        bytes32 root,
        bytes32 nullifier,
        address recipient,
        uint256 amount
    ) external nonReentrant {
        if (nullifiers[nullifier]) revert NullifierAlreadyUsed();
        if (recipient == address(0)) revert ZeroAddress();

        // Check root validity
        (bool isValid, uint256 sourceChainId) = isValidRoot(root);
        if (!isValid) revert InvalidRoot();

        // Check liquidity
        if (token.balanceOf(address(this)) < amount) revert InsufficientLiquidity();

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
                abi.encodeWithSignature("verifyProof(uint256[8],uint256[4])", proofData, publicInputs)
            );

            if (!success || (result.length > 0 && !abi.decode(result, (bool)))) {
                revert InvalidProof();
            }
        }

        nullifiers[nullifier] = true;

        // Higher fee for cross-chain
        uint256 feeBps = (sourceChainId != block.chainid) ? bridgeFeeBps : withdrawFeeBps;
        uint256 fee = (amount * feeBps) / 10000;
        uint256 netAmount = amount - fee;

        // Track stats
        totalWithdrawn += amount;
        if (sourceChainId != block.chainid) {
            totalBridgedIn += amount;
        }

        token.safeTransfer(recipient, netAmount);
        if (fee > 0) {
            token.safeTransfer(feeRecipient, fee);
        }

        emit Withdrawal(nullifier, recipient, netAmount, sourceChainId);
    }

    // ============ Liquidity Management ============

    /**
     * @notice Add liquidity to the pool (for bridging)
     */
    function addLiquidity(uint256 amount) external {
        token.safeTransferFrom(msg.sender, address(this), amount);
    }

    /**
     * @notice Emergency withdraw liquidity (owner only)
     */
    function emergencyWithdraw(uint256 amount) external onlyOwner {
        token.safeTransfer(owner(), amount);
    }

    // ============ View Functions ============

    function getLastRoot() public view returns (bytes32) {
        return roots[roots.length - 1];
    }

    function getExternalRootCount() external view returns (uint256) {
        return externalRootList.length;
    }

    function getLiquidity() external view returns (uint256) {
        return token.balanceOf(address(this));
    }

    function getStats() external view returns (
        uint256 deposited,
        uint256 withdrawn,
        uint256 bridgedIn,
        uint256 commitmentCount,
        bytes32 currentRoot,
        uint256 liquidity
    ) {
        return (
            totalDeposited,
            totalWithdrawn,
            totalBridgedIn,
            nextIndex,
            getLastRoot(),
            token.balanceOf(address(this))
        );
    }

    // ============ Admin Functions ============

    function setRelayer(address relayer, bool authorized) external onlyOwner {
        relayers[relayer] = authorized;
        emit RelayerUpdated(relayer, authorized);
    }

    function setVerifier(address _verifier) external onlyOwner {
        verifier = _verifier;
    }

    function setFees(uint256 _depositFeeBps, uint256 _withdrawFeeBps, uint256 _bridgeFeeBps) external onlyOwner {
        require(_depositFeeBps <= 100 && _withdrawFeeBps <= 100 && _bridgeFeeBps <= 100, "Fee too high");
        depositFeeBps = _depositFeeBps;
        withdrawFeeBps = _withdrawFeeBps;
        bridgeFeeBps = _bridgeFeeBps;
    }

    function setFeeRecipient(address _feeRecipient) external onlyOwner {
        if (_feeRecipient == address(0)) revert ZeroAddress();
        feeRecipient = _feeRecipient;
    }
}
