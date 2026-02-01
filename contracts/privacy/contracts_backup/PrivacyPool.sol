// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "@openzeppelin/contracts/token/ERC20/IERC20.sol";
import "@openzeppelin/contracts/token/ERC20/utils/SafeERC20.sol";
import "@openzeppelin/contracts/security/ReentrancyGuard.sol";
import "@openzeppelin/contracts/access/Ownable.sol";
import "@layerzerolabs/lz-evm-oapp-v2/contracts/oapp/OApp.sol";

/**
 * @title PrivacyPool
 * @notice Liquidity pool for cross-chain private transfers
 * @dev Deployed on each supported chain (Ethereum, BSC, Polygon, etc.)
 *
 * Flow:
 * 1. User deposits USDT → locked in this pool
 * 2. LayerZero message sent to WATTx Privacy Controller
 * 3. User receives shielded balance on WATTx (private)
 * 4. User can withdraw to ANY chain by providing ZK proof
 * 5. This pool (or another chain's pool) releases USDT
 */
contract PrivacyPool is OApp, ReentrancyGuard {
    using SafeERC20 for IERC20;

    // ============ Constants ============

    uint8 public constant MSG_DEPOSIT = 1;
    uint8 public constant MSG_WITHDRAW = 2;
    uint8 public constant MSG_REBALANCE = 3;

    // Fixed denominations for anonymity (amounts in 6 decimals for USDT)
    uint256 public constant DENOM_100 = 100 * 1e6;      // 100 USDT
    uint256 public constant DENOM_1000 = 1000 * 1e6;    // 1,000 USDT
    uint256 public constant DENOM_10000 = 10000 * 1e6;  // 10,000 USDT
    uint256 public constant DENOM_100000 = 100000 * 1e6; // 100,000 USDT

    // ============ State Variables ============

    IERC20 public immutable usdt;
    uint32 public immutable wattxChainId;  // LayerZero endpoint ID for WATTx

    uint256 public totalDeposited;
    uint256 public totalWithdrawn;
    uint256 public poolBalance;

    // Deposit tracking (for emergency recovery only - not used in normal operation)
    mapping(bytes32 => bool) public processedDeposits;
    mapping(bytes32 => bool) public processedWithdrawals;

    // Fee configuration
    uint256 public depositFeeBps = 10;    // 0.1%
    uint256 public withdrawFeeBps = 10;   // 0.1%
    address public feeRecipient;

    // ============ Events ============

    event Deposited(
        address indexed depositor,
        uint256 amount,
        bytes32 indexed commitmentHash,
        uint256 timestamp
    );

    event WithdrawalProcessed(
        bytes32 indexed nullifier,
        address indexed recipient,
        uint256 amount,
        uint256 timestamp
    );

    event LiquidityAdded(address indexed provider, uint256 amount);
    event LiquidityRemoved(address indexed provider, uint256 amount);
    event FeesCollected(address indexed recipient, uint256 amount);
    event RebalanceReceived(uint32 indexed srcChainId, uint256 amount);

    // ============ Errors ============

    error InvalidDenomination();
    error InsufficientLiquidity();
    error InvalidProof();
    error NullifierAlreadyUsed();
    error UnauthorizedCaller();
    error ZeroAddress();
    error ZeroAmount();

    // ============ Constructor ============

    constructor(
        address _usdt,
        address _lzEndpoint,
        uint32 _wattxChainId,
        address _owner
    ) OApp(_lzEndpoint, _owner) Ownable(_owner) {
        if (_usdt == address(0)) revert ZeroAddress();
        usdt = IERC20(_usdt);
        wattxChainId = _wattxChainId;
        feeRecipient = _owner;
    }

    // ============ Deposit Functions ============

    /**
     * @notice Deposit USDT to receive shielded balance on WATTx
     * @param amount Amount of USDT to deposit (must be valid denomination)
     * @param stealthPubKeyX X coordinate of stealth public key
     * @param stealthPubKeyY Y coordinate of stealth public key
     */
    function deposit(
        uint256 amount,
        bytes32 stealthPubKeyX,
        bytes32 stealthPubKeyY
    ) external payable nonReentrant {
        _validateDenomination(amount);

        // Calculate fee
        uint256 fee = (amount * depositFeeBps) / 10000;
        uint256 netAmount = amount - fee;

        // Transfer USDT from user
        usdt.safeTransferFrom(msg.sender, address(this), amount);

        // Update state
        poolBalance += netAmount;
        totalDeposited += netAmount;

        // Collect fee
        if (fee > 0) {
            usdt.safeTransfer(feeRecipient, fee);
            emit FeesCollected(feeRecipient, fee);
        }

        // Create commitment hash (for tracking)
        bytes32 commitmentHash = keccak256(abi.encodePacked(
            msg.sender,
            netAmount,
            stealthPubKeyX,
            stealthPubKeyY,
            block.timestamp
        ));

        // Send message to WATTx Privacy Controller
        bytes memory payload = abi.encode(
            MSG_DEPOSIT,
            netAmount,
            stealthPubKeyX,
            stealthPubKeyY,
            commitmentHash
        );

        _lzSend(
            wattxChainId,
            payload,
            _buildOptions(200000), // gas limit for destination
            MessagingFee(msg.value, 0),
            payable(msg.sender)
        );

        emit Deposited(msg.sender, netAmount, commitmentHash, block.timestamp);
    }

    /**
     * @notice Deposit with specific denomination
     */
    function deposit100(bytes32 stealthPubKeyX, bytes32 stealthPubKeyY) external payable {
        this.deposit(DENOM_100, stealthPubKeyX, stealthPubKeyY);
    }

    function deposit1000(bytes32 stealthPubKeyX, bytes32 stealthPubKeyY) external payable {
        this.deposit(DENOM_1000, stealthPubKeyX, stealthPubKeyY);
    }

    function deposit10000(bytes32 stealthPubKeyX, bytes32 stealthPubKeyY) external payable {
        this.deposit(DENOM_10000, stealthPubKeyX, stealthPubKeyY);
    }

    // ============ LayerZero Receive ============

    /**
     * @notice Handle incoming messages from WATTx (withdrawal requests)
     */
    function _lzReceive(
        Origin calldata _origin,
        bytes32 /*_guid*/,
        bytes calldata _payload,
        address /*_executor*/,
        bytes calldata /*_extraData*/
    ) internal override {
        // Only accept messages from WATTx chain
        require(_origin.srcEid == wattxChainId, "Invalid source chain");

        (uint8 msgType, bytes memory data) = abi.decode(_payload, (uint8, bytes));

        if (msgType == MSG_WITHDRAW) {
            _processWithdrawal(data);
        } else if (msgType == MSG_REBALANCE) {
            _processRebalance(_origin.srcEid, data);
        }
    }

    /**
     * @notice Process withdrawal request from WATTx
     */
    function _processWithdrawal(bytes memory data) internal {
        (
            bytes32 nullifier,
            address recipient,
            uint256 amount
        ) = abi.decode(data, (bytes32, address, uint256));

        if (processedWithdrawals[nullifier]) revert NullifierAlreadyUsed();
        if (poolBalance < amount) revert InsufficientLiquidity();
        if (recipient == address(0)) revert ZeroAddress();

        // Mark nullifier as used
        processedWithdrawals[nullifier] = true;

        // Calculate fee
        uint256 fee = (amount * withdrawFeeBps) / 10000;
        uint256 netAmount = amount - fee;

        // Update state
        poolBalance -= amount;
        totalWithdrawn += amount;

        // Transfer to recipient
        usdt.safeTransfer(recipient, netAmount);

        // Collect fee
        if (fee > 0) {
            usdt.safeTransfer(feeRecipient, fee);
            emit FeesCollected(feeRecipient, fee);
        }

        emit WithdrawalProcessed(nullifier, recipient, netAmount, block.timestamp);
    }

    /**
     * @notice Process rebalance from another pool
     */
    function _processRebalance(uint32 srcChainId, bytes memory data) internal {
        (uint256 amount) = abi.decode(data, (uint256));
        poolBalance += amount;
        emit RebalanceReceived(srcChainId, amount);
    }

    // ============ Liquidity Provider Functions ============

    /**
     * @notice Add liquidity to the pool (for LPs)
     */
    function addLiquidity(uint256 amount) external nonReentrant {
        if (amount == 0) revert ZeroAmount();
        usdt.safeTransferFrom(msg.sender, address(this), amount);
        poolBalance += amount;
        emit LiquidityAdded(msg.sender, amount);
    }

    /**
     * @notice Remove liquidity from pool (owner only, for rebalancing)
     */
    function removeLiquidity(uint256 amount, address to) external onlyOwner nonReentrant {
        if (amount > poolBalance) revert InsufficientLiquidity();
        poolBalance -= amount;
        usdt.safeTransfer(to, amount);
        emit LiquidityRemoved(to, amount);
    }

    // ============ Admin Functions ============

    function setFees(uint256 _depositFeeBps, uint256 _withdrawFeeBps) external onlyOwner {
        require(_depositFeeBps <= 100 && _withdrawFeeBps <= 100, "Fee too high"); // Max 1%
        depositFeeBps = _depositFeeBps;
        withdrawFeeBps = _withdrawFeeBps;
    }

    function setFeeRecipient(address _feeRecipient) external onlyOwner {
        if (_feeRecipient == address(0)) revert ZeroAddress();
        feeRecipient = _feeRecipient;
    }

    // ============ View Functions ============

    function getPoolStats() external view returns (
        uint256 balance,
        uint256 deposited,
        uint256 withdrawn,
        uint256 available
    ) {
        return (poolBalance, totalDeposited, totalWithdrawn, usdt.balanceOf(address(this)));
    }

    function isValidDenomination(uint256 amount) public pure returns (bool) {
        return amount == DENOM_100 ||
               amount == DENOM_1000 ||
               amount == DENOM_10000 ||
               amount == DENOM_100000;
    }

    function quoteDeposit(uint256 amount) external view returns (uint256 fee, uint256 lzFee) {
        fee = (amount * depositFeeBps) / 10000;
        // LZ fee estimation would go here
        lzFee = 0; // Placeholder
    }

    // ============ Internal Functions ============

    function _validateDenomination(uint256 amount) internal pure {
        if (!isValidDenomination(amount)) revert InvalidDenomination();
    }

    function _buildOptions(uint128 gasLimit) internal pure returns (bytes memory) {
        return abi.encodePacked(uint16(1), gasLimit);
    }
}
