// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/ReentrancyGuard.sol";
import "../interfaces/IWATT.sol";
import "../interfaces/IPoolRegistry.sol";

/**
 * @title PoolRegistry
 * @dev Registry for mining pool operators on Polygon and Altcoinchain
 *
 * Requirements for pool operators:
 * 1. Stake 100,000 WATT tokens
 * 2. Run a WATTx node (provide node endpoint)
 * 3. Send regular heartbeats to prove node is online
 * 4. 90-day unstaking hold period
 *
 * NFT miners can view pool fees/terms and node status before joining.
 */
contract PoolRegistry is IPoolRegistry, Ownable, ReentrancyGuard {
    // ============================================================================
    // Constants
    // ============================================================================

    uint256 public constant REQUIRED_STAKE = 100_000 * 1e18;  // 100,000 WATT
    uint256 public constant UNSTAKE_HOLD_PERIOD = 90 days;
    uint256 public constant MAX_FEE_PERCENT = 1000;  // 10% max fee
    uint8 public constant MAX_ALGORITHMS = 7;
    uint256 public constant NODE_HEARTBEAT_INTERVAL = 1 hours;  // Must heartbeat every hour
    uint256 public constant NODE_OFFLINE_THRESHOLD = 4 hours;   // Considered offline after 4 hours

    // ============================================================================
    // Extended Structs
    // ============================================================================

    /// @notice WATTx node information
    struct NodeInfo {
        string rpcEndpoint;         // WATTx node RPC endpoint (e.g., "http://node.pool.com:1337")
        string p2pEndpoint;         // P2P endpoint (e.g., "enode://...@ip:port")
        uint256 lastHeartbeat;      // Last heartbeat timestamp
        uint256 reportedBlockHeight; // Block height reported in last heartbeat
        bool verified;              // Has node been verified by admin
        uint256 uptime;             // Total seconds of verified uptime
        uint256 downtimeCount;      // Number of times node went offline
    }

    // ============================================================================
    // State Variables
    // ============================================================================

    IWATT public immutable wattToken;

    // Operator address => Pool info
    mapping(address => PoolInfo) public pools;

    // Operator address => Node info
    mapping(address => NodeInfo) public nodeInfo;

    // List of all registered operators
    address[] public registeredOperators;

    // Algorithm => List of operators supporting it
    mapping(uint8 => address[]) public algorithmOperators;

    // Operator => index in registeredOperators array
    mapping(address => uint256) private operatorIndex;

    // Track if operator was ever registered (for index validity)
    mapping(address => bool) private everRegistered;

    // Total staked across all pools
    uint256 public totalStaked;

    // Verified node operators (can submit heartbeats via relayer)
    mapping(address => bool) public heartbeatRelayers;

    // ============================================================================
    // Events (additional events not in interface)
    // ============================================================================

    event NodeUpdated(address indexed operator, string rpcEndpoint, string p2pEndpoint);
    event RelayerUpdated(address indexed relayer, bool authorized);

    // ============================================================================
    // Constructor
    // ============================================================================

    constructor(address _wattToken) Ownable(msg.sender) {
        require(_wattToken != address(0), "Invalid WATT address");
        wattToken = IWATT(_wattToken);
    }

    // ============================================================================
    // Registration Functions
    // ============================================================================

    /**
     * @dev Register a new mining pool with WATTx node
     * Requires approval of REQUIRED_STAKE WATT tokens first
     * @param name Pool name
     * @param description Pool description
     * @param stratumEndpoint Stratum endpoint for miners
     * @param nodeRpcEndpoint WATTx node RPC endpoint
     * @param nodeP2pEndpoint WATTx node P2P endpoint
     * @param feePercent Pool fee in basis points
     * @param minPayout Minimum payout threshold
     * @param algorithms Supported algorithm IDs
     */
    function registerPoolWithNode(
        string calldata name,
        string calldata description,
        string calldata stratumEndpoint,
        string calldata nodeRpcEndpoint,
        string calldata nodeP2pEndpoint,
        uint256 feePercent,
        uint256 minPayout,
        uint8[] calldata algorithms
    ) external override nonReentrant {
        require(pools[msg.sender].status == PoolStatus.INACTIVE, "Already registered");
        require(bytes(name).length > 0 && bytes(name).length <= 64, "Invalid name");
        require(bytes(stratumEndpoint).length > 0 && bytes(stratumEndpoint).length <= 256, "Invalid stratum endpoint");
        require(bytes(nodeRpcEndpoint).length > 0 && bytes(nodeRpcEndpoint).length <= 256, "Invalid node RPC endpoint");
        require(bytes(nodeP2pEndpoint).length > 0 && bytes(nodeP2pEndpoint).length <= 512, "Invalid node P2P endpoint");
        require(feePercent <= MAX_FEE_PERCENT, "Fee too high");
        require(algorithms.length > 0 && algorithms.length <= MAX_ALGORITHMS, "Invalid algorithms");

        // Validate algorithms
        for (uint256 i = 0; i < algorithms.length; i++) {
            require(algorithms[i] < MAX_ALGORITHMS, "Invalid algorithm ID");
        }

        // Transfer stake
        require(
            wattToken.transferFrom(msg.sender, address(this), REQUIRED_STAKE),
            "Stake transfer failed"
        );

        // Create pool
        pools[msg.sender] = PoolInfo({
            operator: msg.sender,
            name: name,
            description: description,
            endpoint: stratumEndpoint,
            feePercent: feePercent,
            minPayout: minPayout,
            stakedAmount: REQUIRED_STAKE,
            registeredAt: block.timestamp,
            unstakeRequestedAt: 0,
            status: PoolStatus.ACTIVE,
            supportedAlgorithms: algorithms
        });

        // Register node
        nodeInfo[msg.sender] = NodeInfo({
            rpcEndpoint: nodeRpcEndpoint,
            p2pEndpoint: nodeP2pEndpoint,
            lastHeartbeat: block.timestamp,
            reportedBlockHeight: 0,
            verified: false,
            uptime: 0,
            downtimeCount: 0
        });

        // Add to registered list
        if (!everRegistered[msg.sender]) {
            operatorIndex[msg.sender] = registeredOperators.length;
            registeredOperators.push(msg.sender);
            everRegistered[msg.sender] = true;
        }

        // Add to algorithm lists
        for (uint256 i = 0; i < algorithms.length; i++) {
            algorithmOperators[algorithms[i]].push(msg.sender);
        }

        totalStaked += REQUIRED_STAKE;

        emit PoolRegistered(msg.sender, name, feePercent);
        emit NodeRegistered(msg.sender, nodeRpcEndpoint, nodeP2pEndpoint);
    }

    /**
     * @dev Legacy register function (calls new one with empty node endpoints)
     */
    function registerPool(
        string calldata name,
        string calldata description,
        string calldata endpoint,
        uint256 feePercent,
        uint256 minPayout,
        uint8[] calldata algorithms
    ) external override {
        revert("Use registerPool with node endpoints");
    }

    /**
     * @dev Update pool information
     */
    function updatePool(
        string calldata name,
        string calldata description,
        string calldata endpoint,
        uint256 feePercent,
        uint256 minPayout,
        uint8[] calldata algorithms
    ) external override nonReentrant {
        PoolInfo storage pool = pools[msg.sender];
        require(pool.status == PoolStatus.ACTIVE || pool.status == PoolStatus.PAUSED, "Pool not active");
        require(bytes(name).length > 0 && bytes(name).length <= 64, "Invalid name");
        require(bytes(endpoint).length > 0 && bytes(endpoint).length <= 256, "Invalid endpoint");
        require(feePercent <= MAX_FEE_PERCENT, "Fee too high");
        require(algorithms.length > 0 && algorithms.length <= MAX_ALGORITHMS, "Invalid algorithms");

        // Remove from old algorithm lists
        _removeFromAlgorithmLists(msg.sender, pool.supportedAlgorithms);

        // Update pool
        pool.name = name;
        pool.description = description;
        pool.endpoint = endpoint;
        pool.feePercent = feePercent;
        pool.minPayout = minPayout;
        pool.supportedAlgorithms = algorithms;

        // Add to new algorithm lists
        for (uint256 i = 0; i < algorithms.length; i++) {
            algorithmOperators[algorithms[i]].push(msg.sender);
        }

        emit PoolUpdated(msg.sender, name, feePercent);
    }

    /**
     * @dev Update node endpoints
     */
    function updateNode(
        string calldata rpcEndpoint,
        string calldata p2pEndpoint
    ) external {
        require(pools[msg.sender].status == PoolStatus.ACTIVE || pools[msg.sender].status == PoolStatus.PAUSED, "Pool not active");
        require(bytes(rpcEndpoint).length > 0, "Invalid RPC endpoint");
        require(bytes(p2pEndpoint).length > 0, "Invalid P2P endpoint");

        NodeInfo storage node = nodeInfo[msg.sender];
        node.rpcEndpoint = rpcEndpoint;
        node.p2pEndpoint = p2pEndpoint;
        node.verified = false;  // Requires re-verification after update

        emit NodeUpdated(msg.sender, rpcEndpoint, p2pEndpoint);
    }

    // ============================================================================
    // Node Heartbeat Functions
    // ============================================================================

    /**
     * @dev Submit heartbeat to prove node is online
     * Called by pool operator or authorized relayer
     * @param operator Pool operator address (if called by relayer)
     * @param blockHeight Current WATTx block height on the node
     */
    function submitHeartbeat(address operator, uint256 blockHeight) external {
        // Allow operator or authorized relayer
        require(
            msg.sender == operator || heartbeatRelayers[msg.sender],
            "Not authorized"
        );

        PoolInfo storage pool = pools[operator];
        require(
            pool.status == PoolStatus.ACTIVE || pool.status == PoolStatus.PAUSED,
            "Pool not active"
        );

        NodeInfo storage node = nodeInfo[operator];

        // Calculate uptime since last heartbeat
        if (node.lastHeartbeat > 0 && block.timestamp - node.lastHeartbeat <= NODE_OFFLINE_THRESHOLD) {
            node.uptime += block.timestamp - node.lastHeartbeat;
        } else if (node.lastHeartbeat > 0) {
            // Node was offline
            node.downtimeCount++;
            emit NodeOffline(operator, node.lastHeartbeat);
        }

        node.lastHeartbeat = block.timestamp;
        node.reportedBlockHeight = blockHeight;

        emit NodeHeartbeat(operator, blockHeight, block.timestamp);
    }

    /**
     * @dev Batch heartbeat submission (for relayers)
     */
    function submitHeartbeats(
        address[] calldata operators,
        uint256[] calldata blockHeights
    ) external {
        require(heartbeatRelayers[msg.sender], "Not a relayer");
        require(operators.length == blockHeights.length, "Length mismatch");

        for (uint256 i = 0; i < operators.length; i++) {
            address operator = operators[i];
            PoolInfo storage pool = pools[operator];

            if (pool.status != PoolStatus.ACTIVE && pool.status != PoolStatus.PAUSED) {
                continue;
            }

            NodeInfo storage node = nodeInfo[operator];

            if (node.lastHeartbeat > 0 && block.timestamp - node.lastHeartbeat <= NODE_OFFLINE_THRESHOLD) {
                node.uptime += block.timestamp - node.lastHeartbeat;
            } else if (node.lastHeartbeat > 0) {
                node.downtimeCount++;
                emit NodeOffline(operator, node.lastHeartbeat);
            }

            node.lastHeartbeat = block.timestamp;
            node.reportedBlockHeight = blockHeights[i];

            emit NodeHeartbeat(operator, blockHeights[i], block.timestamp);
        }
    }

    // ============================================================================
    // Pool Control Functions
    // ============================================================================

    function pausePool() external override {
        PoolInfo storage pool = pools[msg.sender];
        require(pool.status == PoolStatus.ACTIVE, "Pool not active");

        pool.status = PoolStatus.PAUSED;
        emit PoolPaused(msg.sender);
    }

    function resumePool() external override {
        PoolInfo storage pool = pools[msg.sender];
        require(pool.status == PoolStatus.PAUSED, "Pool not paused");

        // Check node is online before resuming
        require(isNodeOnline(msg.sender), "Node must be online to resume");

        pool.status = PoolStatus.ACTIVE;
        emit PoolResumed(msg.sender);
    }

    // ============================================================================
    // Unstaking Functions
    // ============================================================================

    function requestUnstake() external override {
        PoolInfo storage pool = pools[msg.sender];
        require(
            pool.status == PoolStatus.ACTIVE || pool.status == PoolStatus.PAUSED,
            "Cannot unstake"
        );

        pool.status = PoolStatus.UNSTAKING;
        pool.unstakeRequestedAt = block.timestamp;

        // Remove from algorithm lists
        _removeFromAlgorithmLists(msg.sender, pool.supportedAlgorithms);

        uint256 unlockTime = block.timestamp + UNSTAKE_HOLD_PERIOD;
        emit UnstakeRequested(msg.sender, unlockTime);
    }

    function cancelUnstake() external override {
        PoolInfo storage pool = pools[msg.sender];
        require(pool.status == PoolStatus.UNSTAKING, "Not unstaking");

        // Check node is online before reactivating
        require(isNodeOnline(msg.sender), "Node must be online to reactivate");

        pool.status = PoolStatus.ACTIVE;
        pool.unstakeRequestedAt = 0;

        // Re-add to algorithm lists
        for (uint256 i = 0; i < pool.supportedAlgorithms.length; i++) {
            algorithmOperators[pool.supportedAlgorithms[i]].push(msg.sender);
        }

        emit UnstakeCancelled(msg.sender);
    }

    function completeUnstake() external override nonReentrant {
        PoolInfo storage pool = pools[msg.sender];
        require(pool.status == PoolStatus.UNSTAKING, "Not unstaking");
        require(
            block.timestamp >= pool.unstakeRequestedAt + UNSTAKE_HOLD_PERIOD,
            "Hold period not complete"
        );

        uint256 amount = pool.stakedAmount;
        pool.stakedAmount = 0;
        pool.status = PoolStatus.CLOSED;
        totalStaked -= amount;

        // Transfer stake back
        require(wattToken.transfer(msg.sender, amount), "Transfer failed");

        emit UnstakeCompleted(msg.sender, amount);
    }

    // ============================================================================
    // View Functions
    // ============================================================================

    function getPool(address operator) external view override returns (PoolInfo memory) {
        return pools[operator];
    }

    function getNode(address operator) external view returns (NodeInfo memory) {
        return nodeInfo[operator];
    }

    function isNodeOnline(address operator) public view returns (bool) {
        NodeInfo storage node = nodeInfo[operator];
        return node.lastHeartbeat > 0 &&
               block.timestamp - node.lastHeartbeat <= NODE_OFFLINE_THRESHOLD;
    }

    function isNodeVerified(address operator) external view returns (bool) {
        return nodeInfo[operator].verified;
    }

    function getNodeUptime(address operator) external view returns (uint256 totalUptime, uint256 downtimeCount) {
        NodeInfo storage node = nodeInfo[operator];
        return (node.uptime, node.downtimeCount);
    }

    function getTimeSinceLastHeartbeat(address operator) external view returns (uint256) {
        NodeInfo storage node = nodeInfo[operator];
        if (node.lastHeartbeat == 0) return type(uint256).max;
        return block.timestamp - node.lastHeartbeat;
    }

    function getActivePools() external view override returns (address[] memory operators, PoolInfo[] memory poolList) {
        // Count active pools with online nodes
        uint256 activeCount = 0;
        for (uint256 i = 0; i < registeredOperators.length; i++) {
            address op = registeredOperators[i];
            if (pools[op].status == PoolStatus.ACTIVE && isNodeOnline(op)) {
                activeCount++;
            }
        }

        // Build arrays
        operators = new address[](activeCount);
        poolList = new PoolInfo[](activeCount);

        uint256 j = 0;
        for (uint256 i = 0; i < registeredOperators.length; i++) {
            address op = registeredOperators[i];
            if (pools[op].status == PoolStatus.ACTIVE && isNodeOnline(op)) {
                operators[j] = op;
                poolList[j] = pools[op];
                j++;
            }
        }
    }

    function getPoolsByAlgorithm(uint8 algorithm)
        external view override returns (address[] memory operators, PoolInfo[] memory poolList)
    {
        require(algorithm < MAX_ALGORITHMS, "Invalid algorithm");

        address[] storage algoOps = algorithmOperators[algorithm];

        // Count active pools with online nodes
        uint256 activeCount = 0;
        for (uint256 i = 0; i < algoOps.length; i++) {
            address op = algoOps[i];
            if (pools[op].status == PoolStatus.ACTIVE && isNodeOnline(op)) {
                activeCount++;
            }
        }

        // Build arrays
        operators = new address[](activeCount);
        poolList = new PoolInfo[](activeCount);

        uint256 j = 0;
        for (uint256 i = 0; i < algoOps.length; i++) {
            address op = algoOps[i];
            if (pools[op].status == PoolStatus.ACTIVE && isNodeOnline(op)) {
                operators[j] = op;
                poolList[j] = pools[op];
                j++;
            }
        }
    }

    /**
     * @dev Get all pools including offline ones (for operators to see their status)
     */
    function getAllPoolsWithStatus()
        external view returns (
            address[] memory operators,
            PoolInfo[] memory poolList,
            NodeInfo[] memory nodes,
            bool[] memory online
        )
    {
        uint256 count = registeredOperators.length;

        operators = new address[](count);
        poolList = new PoolInfo[](count);
        nodes = new NodeInfo[](count);
        online = new bool[](count);

        for (uint256 i = 0; i < count; i++) {
            address op = registeredOperators[i];
            operators[i] = op;
            poolList[i] = pools[op];
            nodes[i] = nodeInfo[op];
            online[i] = isNodeOnline(op);
        }
    }

    function isRegistered(address operator) external view override returns (bool) {
        return pools[operator].status != PoolStatus.INACTIVE &&
               pools[operator].status != PoolStatus.CLOSED;
    }

    function requiredStake() external pure override returns (uint256) {
        return REQUIRED_STAKE;
    }

    function unstakeHoldPeriod() external pure override returns (uint256) {
        return UNSTAKE_HOLD_PERIOD;
    }

    function getRegisteredOperatorCount() external view returns (uint256) {
        return registeredOperators.length;
    }

    function getUnstakeUnlockTime(address operator) external view returns (uint256) {
        PoolInfo storage pool = pools[operator];
        if (pool.status != PoolStatus.UNSTAKING) return 0;
        return pool.unstakeRequestedAt + UNSTAKE_HOLD_PERIOD;
    }

    function getTimeUntilUnstake(address operator) external view returns (uint256) {
        PoolInfo storage pool = pools[operator];
        if (pool.status != PoolStatus.UNSTAKING) return 0;

        uint256 unlockTime = pool.unstakeRequestedAt + UNSTAKE_HOLD_PERIOD;
        if (block.timestamp >= unlockTime) return 0;
        return unlockTime - block.timestamp;
    }

    // ============================================================================
    // Internal Functions
    // ============================================================================

    function _removeFromAlgorithmLists(address operator, uint8[] storage algorithms) internal {
        for (uint256 i = 0; i < algorithms.length; i++) {
            uint8 algo = algorithms[i];
            address[] storage ops = algorithmOperators[algo];

            for (uint256 j = 0; j < ops.length; j++) {
                if (ops[j] == operator) {
                    ops[j] = ops[ops.length - 1];
                    ops.pop();
                    break;
                }
            }
        }
    }

    // ============================================================================
    // Admin Functions
    // ============================================================================

    /**
     * @dev Verify a node (after manual verification of endpoints)
     */
    function verifyNode(address operator, bool verified) external onlyOwner {
        require(pools[operator].status != PoolStatus.INACTIVE, "Pool not registered");
        nodeInfo[operator].verified = verified;
        emit NodeVerified(operator, verified);
    }

    /**
     * @dev Set heartbeat relayer authorization
     */
    function setHeartbeatRelayer(address relayer, bool authorized) external onlyOwner {
        heartbeatRelayers[relayer] = authorized;
        emit RelayerUpdated(relayer, authorized);
    }

    /**
     * @dev Emergency: force close a malicious pool
     */
    function forceClosePool(address operator) external onlyOwner {
        PoolInfo storage pool = pools[operator];
        require(pool.status != PoolStatus.INACTIVE && pool.status != PoolStatus.CLOSED, "Invalid pool");

        _removeFromAlgorithmLists(operator, pool.supportedAlgorithms);

        uint256 amount = pool.stakedAmount;
        pool.stakedAmount = 0;
        pool.status = PoolStatus.CLOSED;
        totalStaked -= amount;
    }

    /**
     * @dev Withdraw forfeited stakes
     */
    function withdrawForfeitedStakes(address to, uint256 amount) external onlyOwner {
        uint256 available = wattToken.balanceOf(address(this)) - totalStaked;
        require(amount <= available, "Exceeds forfeited amount");
        require(wattToken.transfer(to, amount), "Transfer failed");
    }
}
