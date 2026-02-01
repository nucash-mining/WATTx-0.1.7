// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/**
 * @title IWTXBridge
 * @dev Interface for WTX-WATT Bridge - enables exchange between WTX and WATT tokens
 *
 * WTX (native WATTx coin) can be exchanged for WATT tokens on Polygon and Altcoinchain
 * for use in the NFT mining game system.
 */
interface IWTXBridge {
    /// @notice Swap direction
    enum SwapDirection {
        WTX_TO_WATT,    // Deposit WTX, receive WATT
        WATT_TO_WTX     // Deposit WATT, receive WTX
    }

    /// @notice Swap request status
    enum SwapStatus {
        PENDING,        // Awaiting processing
        COMPLETED,      // Successfully completed
        CANCELLED,      // Cancelled by user or admin
        EXPIRED         // Expired (not processed in time)
    }

    /// @notice Swap request
    struct SwapRequest {
        address user;
        SwapDirection direction;
        uint256 amount;
        uint256 fee;
        uint256 netAmount;      // Amount after fee
        uint256 requestedAt;
        uint256 completedAt;
        SwapStatus status;
        string wtxTxHash;       // WTX chain transaction hash (for verification)
    }

    /// @notice Request swap from WTX to WATT
    /// @param amount Amount of WTX to swap
    /// @param wtxTxHash Transaction hash of WTX deposit on WATTx chain
    function requestWtxToWatt(uint256 amount, string calldata wtxTxHash) external;

    /// @notice Request swap from WATT to WTX
    /// @param amount Amount of WATT to swap
    /// @param wtxAddress WTX address to receive tokens
    function requestWattToWtx(uint256 amount, string calldata wtxAddress) external;

    /// @notice Complete a swap (operator only)
    /// @param requestId Request ID to complete
    function completeSwap(uint256 requestId) external;

    /// @notice Cancel a swap request
    /// @param requestId Request ID to cancel
    function cancelSwap(uint256 requestId) external;

    /// @notice Get swap request by ID
    function getSwapRequest(uint256 requestId) external view returns (SwapRequest memory);

    /// @notice Get user's pending swaps
    function getUserPendingSwaps(address user) external view returns (uint256[] memory requestIds);

    /// @notice Get exchange rate (1 WTX = ? WATT, scaled by 1e18)
    function exchangeRate() external view returns (uint256);

    /// @notice Get swap fee in basis points
    function swapFee() external view returns (uint256);

    /// @notice Get minimum swap amount
    function minSwapAmount() external view returns (uint256);

    // Events
    event SwapRequested(uint256 indexed requestId, address indexed user, SwapDirection direction, uint256 amount);
    event SwapCompleted(uint256 indexed requestId, address indexed user, uint256 netAmount);
    event SwapCancelled(uint256 indexed requestId, address indexed user);
    event ExchangeRateUpdated(uint256 oldRate, uint256 newRate);
    event SwapFeeUpdated(uint256 oldFee, uint256 newFee);
}
