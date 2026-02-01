// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/ReentrancyGuard.sol";
import "@openzeppelin/contracts/utils/Pausable.sol";
import "../interfaces/IWATT.sol";
import "../interfaces/IWTXBridge.sol";

/**
 * @title WTXBridge
 * @dev Bridge for exchanging WTX (native WATTx coin) with WATT tokens
 *
 * Enables seamless exchange between WTX on the WATTx chain and WATT tokens
 * on Polygon and Altcoinchain for use in the NFT mining game.
 *
 * Flow:
 * 1. WTX → WATT: User deposits WTX on WATTx chain, provides tx hash, receives WATT
 * 2. WATT → WTX: User deposits WATT, operator sends WTX to user's address
 *
 * Note: This contract holds WATT liquidity. The bridge operator manages
 * the WTX side off-chain and verifies cross-chain transactions.
 */
contract WTXBridge is IWTXBridge, Ownable, ReentrancyGuard, Pausable {
    // ============================================================================
    // Constants
    // ============================================================================

    uint256 public constant BASIS_POINTS = 10000;
    uint256 public constant MAX_SWAP_FEE = 500;  // 5% max
    uint256 public constant REQUEST_EXPIRY = 7 days;

    // ============================================================================
    // State Variables
    // ============================================================================

    IWATT public immutable wattToken;

    // Exchange rate: 1 WTX = exchangeRate / 1e18 WATT (default 1:1)
    uint256 public exchangeRate = 1e18;

    // Swap fee in basis points (default 0.5%)
    uint256 public swapFee = 50;

    // Minimum swap amount
    uint256 public minSwapAmount = 100 * 1e18;  // 100 tokens

    // Maximum swap amount per request
    uint256 public maxSwapAmount = 1_000_000 * 1e18;  // 1M tokens

    // Daily swap limit per user
    uint256 public dailyUserLimit = 100_000 * 1e18;  // 100K tokens

    // Swap request counter
    uint256 public nextRequestId = 1;

    // Request ID => SwapRequest
    mapping(uint256 => SwapRequest) public swapRequests;

    // User => list of request IDs
    mapping(address => uint256[]) public userRequests;

    // User => day => amount swapped
    mapping(address => mapping(uint256 => uint256)) public userDailySwapped;

    // WTX tx hash => request ID (to prevent double-spend)
    mapping(string => uint256) public wtxTxToRequest;

    // Operators who can complete swaps
    mapping(address => bool) public operators;

    // Bridge liquidity stats
    uint256 public totalWattLiquidity;
    uint256 public totalSwapsCompleted;
    uint256 public totalVolumeWattToWtx;
    uint256 public totalVolumeWtxToWatt;

    // ============================================================================
    // Events
    // ============================================================================

    event LiquidityAdded(address indexed provider, uint256 amount);
    event LiquidityRemoved(address indexed provider, uint256 amount);
    event OperatorUpdated(address indexed operator, bool authorized);
    event DailyLimitUpdated(uint256 oldLimit, uint256 newLimit);
    event MaxSwapUpdated(uint256 oldMax, uint256 newMax);
    event MinSwapUpdated(uint256 oldMin, uint256 newMin);

    // ============================================================================
    // Constructor
    // ============================================================================

    constructor(address _wattToken) Ownable(msg.sender) {
        require(_wattToken != address(0), "Invalid WATT address");
        wattToken = IWATT(_wattToken);
        operators[msg.sender] = true;
        emit OperatorUpdated(msg.sender, true);
    }

    // ============================================================================
    // Modifiers
    // ============================================================================

    modifier onlyOperator() {
        require(operators[msg.sender] || msg.sender == owner(), "Not operator");
        _;
    }

    // ============================================================================
    // Swap Functions
    // ============================================================================

    /**
     * @dev Request swap from WTX to WATT
     * User must have already sent WTX to the bridge address on WATTx chain
     * @param amount Amount of WTX sent
     * @param wtxTxHash Transaction hash of WTX transfer on WATTx chain
     */
    function requestWtxToWatt(uint256 amount, string calldata wtxTxHash)
        external nonReentrant whenNotPaused
    {
        require(amount >= minSwapAmount, "Below minimum");
        require(amount <= maxSwapAmount, "Exceeds maximum");
        require(bytes(wtxTxHash).length > 0, "Invalid tx hash");
        require(wtxTxToRequest[wtxTxHash] == 0, "Tx already used");

        // Check daily limit
        uint256 today = block.timestamp / 1 days;
        require(
            userDailySwapped[msg.sender][today] + amount <= dailyUserLimit,
            "Daily limit exceeded"
        );

        // Calculate amounts
        uint256 wattAmount = (amount * exchangeRate) / 1e18;
        uint256 fee = (wattAmount * swapFee) / BASIS_POINTS;
        uint256 netAmount = wattAmount - fee;

        // Check liquidity
        require(netAmount <= totalWattLiquidity, "Insufficient liquidity");

        // Create request
        uint256 requestId = nextRequestId++;
        swapRequests[requestId] = SwapRequest({
            user: msg.sender,
            direction: SwapDirection.WTX_TO_WATT,
            amount: amount,
            fee: fee,
            netAmount: netAmount,
            requestedAt: block.timestamp,
            completedAt: 0,
            status: SwapStatus.PENDING,
            wtxTxHash: wtxTxHash
        });

        userRequests[msg.sender].push(requestId);
        wtxTxToRequest[wtxTxHash] = requestId;
        userDailySwapped[msg.sender][today] += amount;

        emit SwapRequested(requestId, msg.sender, SwapDirection.WTX_TO_WATT, amount);
    }

    /**
     * @dev Request swap from WATT to WTX
     * WATT tokens are transferred to bridge immediately
     * @param amount Amount of WATT to swap
     * @param wtxAddress WTX address to receive tokens on WATTx chain
     */
    function requestWattToWtx(uint256 amount, string calldata wtxAddress)
        external nonReentrant whenNotPaused
    {
        require(amount >= minSwapAmount, "Below minimum");
        require(amount <= maxSwapAmount, "Exceeds maximum");
        require(bytes(wtxAddress).length > 0, "Invalid WTX address");

        // Check daily limit
        uint256 today = block.timestamp / 1 days;
        require(
            userDailySwapped[msg.sender][today] + amount <= dailyUserLimit,
            "Daily limit exceeded"
        );

        // Transfer WATT from user
        require(
            wattToken.transferFrom(msg.sender, address(this), amount),
            "Transfer failed"
        );

        // Calculate amounts
        uint256 fee = (amount * swapFee) / BASIS_POINTS;
        uint256 wtxAmount = ((amount - fee) * 1e18) / exchangeRate;

        // Create request
        uint256 requestId = nextRequestId++;
        swapRequests[requestId] = SwapRequest({
            user: msg.sender,
            direction: SwapDirection.WATT_TO_WTX,
            amount: amount,
            fee: fee,
            netAmount: wtxAmount,
            requestedAt: block.timestamp,
            completedAt: 0,
            status: SwapStatus.PENDING,
            wtxTxHash: wtxAddress  // Storing WTX address here
        });

        userRequests[msg.sender].push(requestId);
        userDailySwapped[msg.sender][today] += amount;

        // Add to liquidity pool (minus fee)
        totalWattLiquidity += (amount - fee);

        emit SwapRequested(requestId, msg.sender, SwapDirection.WATT_TO_WTX, amount);
    }

    /**
     * @dev Complete a swap request (operator only)
     */
    function completeSwap(uint256 requestId) external onlyOperator nonReentrant {
        SwapRequest storage request = swapRequests[requestId];
        require(request.status == SwapStatus.PENDING, "Invalid status");
        require(block.timestamp <= request.requestedAt + REQUEST_EXPIRY, "Request expired");

        request.status = SwapStatus.COMPLETED;
        request.completedAt = block.timestamp;
        totalSwapsCompleted++;

        if (request.direction == SwapDirection.WTX_TO_WATT) {
            // Send WATT to user
            totalWattLiquidity -= request.netAmount;
            require(wattToken.transfer(request.user, request.netAmount), "Transfer failed");
            totalVolumeWtxToWatt += request.amount;
        } else {
            // WATT already received, WTX sent off-chain by operator
            totalVolumeWattToWtx += request.amount;
        }

        emit SwapCompleted(requestId, request.user, request.netAmount);
    }

    /**
     * @dev Cancel a swap request
     * Can be called by user (for WATT→WTX) or operator
     */
    function cancelSwap(uint256 requestId) external nonReentrant {
        SwapRequest storage request = swapRequests[requestId];
        require(request.status == SwapStatus.PENDING, "Invalid status");
        require(
            msg.sender == request.user || operators[msg.sender] || msg.sender == owner(),
            "Not authorized"
        );

        request.status = SwapStatus.CANCELLED;

        // Refund if WATT→WTX (WATT was already transferred)
        if (request.direction == SwapDirection.WATT_TO_WTX) {
            totalWattLiquidity -= (request.amount - request.fee);
            require(wattToken.transfer(request.user, request.amount), "Refund failed");
        }

        emit SwapCancelled(requestId, request.user);
    }

    // ============================================================================
    // Liquidity Functions
    // ============================================================================

    /**
     * @dev Add WATT liquidity to the bridge
     */
    function addLiquidity(uint256 amount) external nonReentrant {
        require(amount > 0, "Zero amount");
        require(wattToken.transferFrom(msg.sender, address(this), amount), "Transfer failed");
        totalWattLiquidity += amount;
        emit LiquidityAdded(msg.sender, amount);
    }

    /**
     * @dev Remove WATT liquidity (owner only)
     */
    function removeLiquidity(uint256 amount) external onlyOwner nonReentrant {
        require(amount <= totalWattLiquidity, "Exceeds liquidity");
        totalWattLiquidity -= amount;
        require(wattToken.transfer(msg.sender, amount), "Transfer failed");
        emit LiquidityRemoved(msg.sender, amount);
    }

    // ============================================================================
    // View Functions
    // ============================================================================

    function getSwapRequest(uint256 requestId) external view returns (SwapRequest memory) {
        return swapRequests[requestId];
    }

    function getUserPendingSwaps(address user) external view returns (uint256[] memory requestIds) {
        uint256[] storage allRequests = userRequests[user];
        uint256 pendingCount = 0;

        // Count pending
        for (uint256 i = 0; i < allRequests.length; i++) {
            if (swapRequests[allRequests[i]].status == SwapStatus.PENDING) {
                pendingCount++;
            }
        }

        // Build array
        requestIds = new uint256[](pendingCount);
        uint256 j = 0;
        for (uint256 i = 0; i < allRequests.length; i++) {
            if (swapRequests[allRequests[i]].status == SwapStatus.PENDING) {
                requestIds[j++] = allRequests[i];
            }
        }
    }

    function getUserAllSwaps(address user) external view returns (uint256[] memory) {
        return userRequests[user];
    }

    function getUserDailyRemaining(address user) external view returns (uint256) {
        uint256 today = block.timestamp / 1 days;
        uint256 used = userDailySwapped[user][today];
        return used >= dailyUserLimit ? 0 : dailyUserLimit - used;
    }

    function getQuote(SwapDirection direction, uint256 amount)
        external view returns (uint256 outputAmount, uint256 fee)
    {
        if (direction == SwapDirection.WTX_TO_WATT) {
            uint256 wattAmount = (amount * exchangeRate) / 1e18;
            fee = (wattAmount * swapFee) / BASIS_POINTS;
            outputAmount = wattAmount - fee;
        } else {
            fee = (amount * swapFee) / BASIS_POINTS;
            outputAmount = ((amount - fee) * 1e18) / exchangeRate;
        }
    }

    function getBridgeStats() external view returns (
        uint256 _liquidity,
        uint256 _totalSwaps,
        uint256 _volumeWattToWtx,
        uint256 _volumeWtxToWatt,
        uint256 _exchangeRate,
        uint256 _swapFee
    ) {
        return (
            totalWattLiquidity,
            totalSwapsCompleted,
            totalVolumeWattToWtx,
            totalVolumeWtxToWatt,
            exchangeRate,
            swapFee
        );
    }

    // ============================================================================
    // Admin Functions
    // ============================================================================

    function setOperator(address operator, bool authorized) external onlyOwner {
        operators[operator] = authorized;
        emit OperatorUpdated(operator, authorized);
    }

    function setExchangeRate(uint256 newRate) external onlyOwner {
        require(newRate > 0, "Invalid rate");
        emit ExchangeRateUpdated(exchangeRate, newRate);
        exchangeRate = newRate;
    }

    function setSwapFee(uint256 newFee) external onlyOwner {
        require(newFee <= MAX_SWAP_FEE, "Fee too high");
        emit SwapFeeUpdated(swapFee, newFee);
        swapFee = newFee;
    }

    function setMinSwapAmount(uint256 newMin) external onlyOwner {
        emit MinSwapUpdated(minSwapAmount, newMin);
        minSwapAmount = newMin;
    }

    function setMaxSwapAmount(uint256 newMax) external onlyOwner {
        emit MaxSwapUpdated(maxSwapAmount, newMax);
        maxSwapAmount = newMax;
    }

    function setDailyUserLimit(uint256 newLimit) external onlyOwner {
        emit DailyLimitUpdated(dailyUserLimit, newLimit);
        dailyUserLimit = newLimit;
    }

    function pause() external onlyOwner {
        _pause();
    }

    function unpause() external onlyOwner {
        _unpause();
    }

    /**
     * @dev Expire old pending requests
     */
    function expireRequests(uint256[] calldata requestIds) external onlyOperator {
        for (uint256 i = 0; i < requestIds.length; i++) {
            SwapRequest storage request = swapRequests[requestIds[i]];
            if (request.status == SwapStatus.PENDING &&
                block.timestamp > request.requestedAt + REQUEST_EXPIRY) {
                request.status = SwapStatus.EXPIRED;

                // Refund WATT→WTX requests
                if (request.direction == SwapDirection.WATT_TO_WTX) {
                    totalWattLiquidity -= (request.amount - request.fee);
                    wattToken.transfer(request.user, request.amount);
                }
            }
        }
    }

    /**
     * @dev Collect accumulated fees
     */
    function collectFees(address to) external onlyOwner {
        uint256 balance = wattToken.balanceOf(address(this));
        uint256 fees = balance - totalWattLiquidity;
        if (fees > 0) {
            require(wattToken.transfer(to, fees), "Transfer failed");
        }
    }
}
