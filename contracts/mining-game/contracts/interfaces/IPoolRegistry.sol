// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/**
 * @title IPoolRegistry
 * @dev Interface for Pool Registry - where mining pool operators register and stake
 *
 * Pool operators must stake 100,000 WATT tokens to register their pool.
 * NFT miners can view pool terms before joining.
 * 90-day unstaking hold period applies.
 */
interface IPoolRegistry {
    /// @notice Pool status
    enum PoolStatus {
        INACTIVE,       // Not registered
        ACTIVE,         // Registered and accepting miners
        PAUSED,         // Temporarily not accepting new miners
        UNSTAKING,      // In 90-day unstaking period
        CLOSED          // Fully unstaked and closed
    }

    /// @notice Pool information
    struct PoolInfo {
        address operator;           // Pool operator address
        string name;                // Pool name
        string description;         // Pool description
        string endpoint;            // Stratum endpoint (e.g., "stratum+tcp://pool.example.com:3333")
        uint256 feePercent;         // Pool fee in basis points (100 = 1%)
        uint256 minPayout;          // Minimum payout threshold (in smallest unit)
        uint256 stakedAmount;       // Amount of WATT staked
        uint256 registeredAt;       // Registration timestamp
        uint256 unstakeRequestedAt; // When unstake was requested (0 if not unstaking)
        PoolStatus status;
        uint8[] supportedAlgorithms; // Which algorithms this pool supports
    }

    /// @notice Register a new mining pool (legacy - use version with node endpoints)
    /// @param name Pool name
    /// @param description Pool description
    /// @param endpoint Stratum endpoint URL
    /// @param feePercent Fee in basis points (max 1000 = 10%)
    /// @param minPayout Minimum payout threshold
    /// @param algorithms Supported algorithm IDs
    function registerPool(
        string calldata name,
        string calldata description,
        string calldata endpoint,
        uint256 feePercent,
        uint256 minPayout,
        uint8[] calldata algorithms
    ) external;

    /// @notice Register a new mining pool with WATTx node requirement
    /// @param name Pool name
    /// @param description Pool description
    /// @param stratumEndpoint Stratum endpoint for miners
    /// @param nodeRpcEndpoint WATTx node RPC endpoint
    /// @param nodeP2pEndpoint WATTx node P2P endpoint
    /// @param feePercent Fee in basis points (max 1000 = 10%)
    /// @param minPayout Minimum payout threshold
    /// @param algorithms Supported algorithm IDs
    function registerPoolWithNode(
        string calldata name,
        string calldata description,
        string calldata stratumEndpoint,
        string calldata nodeRpcEndpoint,
        string calldata nodeP2pEndpoint,
        uint256 feePercent,
        uint256 minPayout,
        uint8[] calldata algorithms
    ) external;

    /// @notice Submit heartbeat to prove node is online
    /// @param operator Pool operator address
    /// @param blockHeight Current WATTx block height
    function submitHeartbeat(address operator, uint256 blockHeight) external;

    /// @notice Check if operator's node is online
    function isNodeOnline(address operator) external view returns (bool);

    /// @notice Update pool information (operator only)
    function updatePool(
        string calldata name,
        string calldata description,
        string calldata endpoint,
        uint256 feePercent,
        uint256 minPayout,
        uint8[] calldata algorithms
    ) external;

    /// @notice Pause pool (stop accepting new miners)
    function pausePool() external;

    /// @notice Resume pool
    function resumePool() external;

    /// @notice Request unstaking (starts 90-day hold period)
    function requestUnstake() external;

    /// @notice Cancel unstake request
    function cancelUnstake() external;

    /// @notice Complete unstake after hold period
    function completeUnstake() external;

    /// @notice Get pool info by operator address
    function getPool(address operator) external view returns (PoolInfo memory);

    /// @notice Get all active pools
    function getActivePools() external view returns (address[] memory operators, PoolInfo[] memory pools);

    /// @notice Get pools supporting a specific algorithm
    function getPoolsByAlgorithm(uint8 algorithm) external view returns (address[] memory operators, PoolInfo[] memory pools);

    /// @notice Check if an operator has a registered pool
    function isRegistered(address operator) external view returns (bool);

    /// @notice Get required stake amount
    function requiredStake() external view returns (uint256);

    /// @notice Get unstaking hold period
    function unstakeHoldPeriod() external view returns (uint256);

    // Events
    event PoolRegistered(address indexed operator, string name, uint256 feePercent);
    event PoolUpdated(address indexed operator, string name, uint256 feePercent);
    event PoolPaused(address indexed operator);
    event PoolResumed(address indexed operator);
    event UnstakeRequested(address indexed operator, uint256 unlockTime);
    event UnstakeCancelled(address indexed operator);
    event UnstakeCompleted(address indexed operator, uint256 amount);
    event NodeRegistered(address indexed operator, string rpcEndpoint, string p2pEndpoint);
    event NodeHeartbeat(address indexed operator, uint256 blockHeight, uint256 timestamp);
    event NodeOffline(address indexed operator, uint256 lastHeartbeat);
    event NodeVerified(address indexed operator, bool verified);
}
