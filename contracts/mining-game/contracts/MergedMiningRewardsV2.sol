// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/utils/ReentrancyGuard.sol";

/**
 * @title MergedMiningRewardsV2
 * @dev Extended rewards contract for WATTx multi-algorithm merged mining
 *
 * Improvements over V1:
 * - Support for pool share aggregation (batch submissions)
 * - Per-algorithm tracking and statistics
 * - Algorithm diversity bonus (rewards underrepresented algorithms)
 * - Epoch-based reward distribution
 * - Integration with Mining Game system
 */
contract MergedMiningRewardsV2 is Ownable, ReentrancyGuard {
    // ============================================================================
    // Token Information (ERC20-like)
    // ============================================================================

    string public constant name = "WATTx Mining Token V2";
    string public constant symbol = "WXTM2";
    uint8 public constant decimals = 18;

    uint256 public totalSupply;
    mapping(address => uint256) public balanceOf;
    mapping(address => mapping(address => uint256)) public allowance;

    // ============================================================================
    // Algorithm Enum (matches C++ and NFT)
    // ============================================================================

    // SHA256D=0, SCRYPT=1, ETHASH=2, RANDOMX=3, EQUIHASH=4, X11=5, KHEAVYHASH=6
    uint8 public constant ALGO_COUNT = 7;

    string[7] public algoNames = [
        "SHA256D",
        "Scrypt",
        "Ethash",
        "RandomX",
        "Equihash",
        "X11",
        "kHeavyHash"
    ];

    // ============================================================================
    // Mining State
    // ============================================================================

    address public owner_;
    mapping(address => bool) public operators;
    mapping(address => bool) public poolOperators;  // Authorized pool servers

    // Base reward per share
    uint256 public rewardPerShare = 1e15; // 0.001 WXTM per share

    // Epoch tracking
    uint256 public currentEpoch;
    uint256 public epochDuration = 1 hours;
    uint256 public lastEpochTime;

    struct EpochData {
        uint256 startTime;
        uint256 endTime;
        uint256 totalShares;
        uint256 totalRewards;
        mapping(uint8 => uint256) algoShares;
        bool finalized;
    }

    mapping(uint256 => EpochData) public epochs;

    // Miner tracking
    // epoch => miner => shares
    mapping(uint256 => mapping(address => uint256)) public minerShares;

    // epoch => miner => algo => shares
    mapping(uint256 => mapping(address => mapping(uint8 => uint256))) public minerAlgoShares;

    // epoch => miner => claimed
    mapping(uint256 => mapping(address => bool)) public rewardsClaimed;

    // Statistics
    struct MinerStats {
        uint256 totalShares;
        uint256 totalRewards;
        uint256 blocksParticipated;
        uint256 lastShareTime;
        mapping(uint8 => uint256) algoShares;
    }
    mapping(address => MinerStats) private minerStats_;

    // Global stats
    uint256 public totalSharesAllTime;
    uint256 public totalRewardsAllTime;
    uint256 public totalMiners;
    mapping(address => bool) public knownMiners;
    mapping(uint8 => uint256) public algoTotalShares;

    // ============================================================================
    // Events
    // ============================================================================

    event Transfer(address indexed from, address indexed to, uint256 value);
    event Approval(address indexed owner, address indexed spender, uint256 value);

    event SharesSubmitted(
        address indexed miner,
        uint256 indexed epoch,
        uint8 algo,
        uint256 shares
    );

    event PoolSharesSubmitted(
        address indexed pool,
        uint256 indexed epoch,
        uint8 algo,
        uint256 minerCount,
        uint256 totalShares
    );

    event EpochFinalized(
        uint256 indexed epoch,
        uint256 totalShares,
        uint256 totalRewards
    );

    event RewardsClaimed(
        address indexed miner,
        uint256 indexed epoch,
        uint256 amount
    );

    event OperatorUpdated(address indexed operator, bool authorized);
    event PoolOperatorUpdated(address indexed pool, bool authorized);
    event RewardPerShareUpdated(uint256 oldValue, uint256 newValue);

    // ============================================================================
    // Constructor
    // ============================================================================

    constructor() Ownable(msg.sender) {
        owner_ = msg.sender;
        operators[msg.sender] = true;
        currentEpoch = 1;
        lastEpochTime = block.timestamp;
        epochs[currentEpoch].startTime = block.timestamp;

        emit OperatorUpdated(msg.sender, true);
    }

    // ============================================================================
    // Modifiers
    // ============================================================================

    modifier onlyOperator() {
        require(operators[msg.sender] || msg.sender == owner(), "Not operator");
        _;
    }

    modifier onlyPoolOperator() {
        require(poolOperators[msg.sender] || operators[msg.sender] || msg.sender == owner(), "Not pool operator");
        _;
    }

    // ============================================================================
    // ERC20 Functions
    // ============================================================================

    function transfer(address to, uint256 amount) external returns (bool) {
        require(to != address(0), "Transfer to zero address");
        require(balanceOf[msg.sender] >= amount, "Insufficient balance");

        balanceOf[msg.sender] -= amount;
        balanceOf[to] += amount;

        emit Transfer(msg.sender, to, amount);
        return true;
    }

    function approve(address spender, uint256 amount) external returns (bool) {
        allowance[msg.sender][spender] = amount;
        emit Approval(msg.sender, spender, amount);
        return true;
    }

    function transferFrom(address from, address to, uint256 amount) external returns (bool) {
        require(to != address(0), "Transfer to zero address");
        require(balanceOf[from] >= amount, "Insufficient balance");
        require(allowance[from][msg.sender] >= amount, "Insufficient allowance");

        balanceOf[from] -= amount;
        balanceOf[to] += amount;
        allowance[from][msg.sender] -= amount;

        emit Transfer(from, to, amount);
        return true;
    }

    // ============================================================================
    // Share Submission (Individual Miners via Stratum)
    // ============================================================================

    /**
     * @dev Submit shares for a single miner
     * Called by stratum server operators
     */
    function submitShares(
        address miner,
        uint8 algo,
        uint256 shares
    ) external onlyOperator {
        require(miner != address(0), "Invalid miner");
        require(algo < ALGO_COUNT, "Invalid algorithm");
        require(shares > 0, "No shares");

        _advanceEpochIfNeeded();

        // Track new miners
        if (!knownMiners[miner]) {
            knownMiners[miner] = true;
            totalMiners++;
        }

        // Apply algorithm bonus
        uint256 effectiveShares = _applyAlgoBonus(shares, algo);

        // Record shares
        minerShares[currentEpoch][miner] += effectiveShares;
        minerAlgoShares[currentEpoch][miner][algo] += effectiveShares;
        epochs[currentEpoch].totalShares += effectiveShares;
        epochs[currentEpoch].algoShares[algo] += effectiveShares;

        // Update stats
        minerStats_[miner].totalShares += effectiveShares;
        minerStats_[miner].lastShareTime = block.timestamp;
        minerStats_[miner].algoShares[algo] += effectiveShares;
        totalSharesAllTime += effectiveShares;
        algoTotalShares[algo] += effectiveShares;

        emit SharesSubmitted(miner, currentEpoch, algo, effectiveShares);
    }

    // ============================================================================
    // Pool Share Aggregation (Batch Submission)
    // ============================================================================

    /**
     * @dev Submit aggregated shares from a pool
     * More gas efficient for pools with many miners
     */
    function submitPoolShares(
        uint8 algo,
        address[] calldata miners,
        uint256[] calldata shares
    ) external onlyPoolOperator {
        require(algo < ALGO_COUNT, "Invalid algorithm");
        require(miners.length == shares.length, "Length mismatch");
        require(miners.length > 0, "Empty submission");

        _advanceEpochIfNeeded();

        uint256 totalBatchShares = 0;

        for (uint256 i = 0; i < miners.length; i++) {
            address miner = miners[i];
            uint256 share = shares[i];

            if (miner == address(0) || share == 0) continue;

            // Track new miners
            if (!knownMiners[miner]) {
                knownMiners[miner] = true;
                totalMiners++;
            }

            // Apply algorithm bonus
            uint256 effectiveShares = _applyAlgoBonus(share, algo);

            // Record shares
            minerShares[currentEpoch][miner] += effectiveShares;
            minerAlgoShares[currentEpoch][miner][algo] += effectiveShares;

            // Update miner stats
            minerStats_[miner].totalShares += effectiveShares;
            minerStats_[miner].lastShareTime = block.timestamp;
            minerStats_[miner].algoShares[algo] += effectiveShares;

            totalBatchShares += effectiveShares;
        }

        // Update epoch and global stats
        epochs[currentEpoch].totalShares += totalBatchShares;
        epochs[currentEpoch].algoShares[algo] += totalBatchShares;
        totalSharesAllTime += totalBatchShares;
        algoTotalShares[algo] += totalBatchShares;

        emit PoolSharesSubmitted(msg.sender, currentEpoch, algo, miners.length, totalBatchShares);
    }

    // ============================================================================
    // Algorithm Bonus
    // ============================================================================

    /**
     * @dev Apply bonus for underrepresented algorithms
     * Encourages algorithm diversity on the network
     */
    function _applyAlgoBonus(uint256 shares, uint8 algo) internal view returns (uint256) {
        uint256 bonus = getAlgorithmBonus(currentEpoch, algo);
        return (shares * bonus) / 100;
    }

    /**
     * @dev Get bonus multiplier for an algorithm
     * Underrepresented algorithms get higher bonuses
     */
    function getAlgorithmBonus(uint256 epoch, uint8 algo) public view returns (uint256) {
        uint256 epochShares = epochs[epoch].totalShares;
        if (epochShares == 0) return 100; // 1x

        uint256 algoShares = epochs[epoch].algoShares[algo];
        uint256 percentage = (algoShares * 100) / epochShares;

        // Bonus for underrepresented algorithms (max 2x)
        if (percentage < 5) return 200;   // < 5% = 2x bonus
        if (percentage < 10) return 150;  // < 10% = 1.5x bonus
        if (percentage < 20) return 125;  // < 20% = 1.25x bonus
        return 100;                        // >= 20% = no bonus
    }

    // ============================================================================
    // Epoch Management
    // ============================================================================

    function _advanceEpochIfNeeded() internal {
        if (block.timestamp >= lastEpochTime + epochDuration) {
            _finalizeEpoch();
        }
    }

    function _finalizeEpoch() internal {
        EpochData storage epoch = epochs[currentEpoch];

        // Calculate rewards
        epoch.totalRewards = epoch.totalShares * rewardPerShare;
        epoch.endTime = block.timestamp;
        epoch.finalized = true;
        totalRewardsAllTime += epoch.totalRewards;

        emit EpochFinalized(currentEpoch, epoch.totalShares, epoch.totalRewards);

        // Start new epoch
        currentEpoch++;
        lastEpochTime = block.timestamp;
        epochs[currentEpoch].startTime = block.timestamp;
    }

    /**
     * @dev Manually finalize current epoch (for testing or emergency)
     */
    function finalizeEpoch() external onlyOperator {
        require(!epochs[currentEpoch].finalized, "Already finalized");
        require(epochs[currentEpoch].totalShares > 0, "No shares");
        _finalizeEpoch();
    }

    // ============================================================================
    // Reward Claims
    // ============================================================================

    /**
     * @dev Claim rewards for a specific epoch
     */
    function claimRewards(uint256 epoch) external nonReentrant {
        require(epochs[epoch].finalized, "Epoch not finalized");
        require(!rewardsClaimed[epoch][msg.sender], "Already claimed");
        require(minerShares[epoch][msg.sender] > 0, "No shares in epoch");

        uint256 shares = minerShares[epoch][msg.sender];
        uint256 reward = shares * rewardPerShare;

        rewardsClaimed[epoch][msg.sender] = true;

        // Mint tokens
        _mint(msg.sender, reward);
        minerStats_[msg.sender].totalRewards += reward;

        emit RewardsClaimed(msg.sender, epoch, reward);
    }

    /**
     * @dev Claim rewards for multiple epochs
     */
    function claimMultipleRewards(uint256[] calldata epochIds) external nonReentrant {
        uint256 totalReward = 0;

        for (uint256 i = 0; i < epochIds.length; i++) {
            uint256 epoch = epochIds[i];

            if (!epochs[epoch].finalized) continue;
            if (rewardsClaimed[epoch][msg.sender]) continue;
            if (minerShares[epoch][msg.sender] == 0) continue;

            uint256 shares = minerShares[epoch][msg.sender];
            uint256 reward = shares * rewardPerShare;

            rewardsClaimed[epoch][msg.sender] = true;
            totalReward += reward;

            emit RewardsClaimed(msg.sender, epoch, reward);
        }

        if (totalReward > 0) {
            _mint(msg.sender, totalReward);
            minerStats_[msg.sender].totalRewards += totalReward;
        }
    }

    // ============================================================================
    // View Functions
    // ============================================================================

    function getMinerStats(address miner) external view returns (
        uint256 totalShares,
        uint256 totalRewards,
        uint256 blocksParticipated,
        uint256 lastShareTime
    ) {
        MinerStats storage stats = minerStats_[miner];
        return (
            stats.totalShares,
            stats.totalRewards,
            stats.blocksParticipated,
            stats.lastShareTime
        );
    }

    function getMinerAlgoShares(address miner, uint8 algo) external view returns (uint256) {
        return minerStats_[miner].algoShares[algo];
    }

    function getEpochInfo(uint256 epoch) external view returns (
        uint256 startTime,
        uint256 endTime,
        uint256 totalShares,
        uint256 totalRewards,
        bool finalized
    ) {
        EpochData storage e = epochs[epoch];
        return (e.startTime, e.endTime, e.totalShares, e.totalRewards, e.finalized);
    }

    function getEpochAlgoShares(uint256 epoch, uint8 algo) external view returns (uint256) {
        return epochs[epoch].algoShares[algo];
    }

    function getMinerEpochShares(uint256 epoch, address miner) external view returns (uint256) {
        return minerShares[epoch][miner];
    }

    function getPendingRewards(address miner) external view returns (uint256 pending, uint256 epochCount) {
        for (uint256 i = 1; i < currentEpoch; i++) {
            if (epochs[i].finalized &&
                !rewardsClaimed[i][miner] &&
                minerShares[i][miner] > 0) {
                pending += minerShares[i][miner] * rewardPerShare;
                epochCount++;
            }
        }
    }

    function getPendingEpochs(address miner, uint256 maxEpochs)
        external view returns (uint256[] memory)
    {
        uint256[] memory temp = new uint256[](maxEpochs);
        uint256 count = 0;

        for (uint256 i = 1; i < currentEpoch && count < maxEpochs; i++) {
            if (epochs[i].finalized &&
                !rewardsClaimed[i][miner] &&
                minerShares[i][miner] > 0) {
                temp[count++] = i;
            }
        }

        uint256[] memory result = new uint256[](count);
        for (uint256 i = 0; i < count; i++) {
            result[i] = temp[i];
        }
        return result;
    }

    function getGlobalStats() external view returns (
        uint256 _totalMiners,
        uint256 _totalSharesAllTime,
        uint256 _totalRewardsAllTime,
        uint256 _currentEpoch,
        uint256 _rewardPerShare
    ) {
        return (totalMiners, totalSharesAllTime, totalRewardsAllTime, currentEpoch, rewardPerShare);
    }

    function getAllAlgoShares() external view returns (uint256[7] memory) {
        uint256[7] memory shares;
        for (uint8 i = 0; i < ALGO_COUNT; i++) {
            shares[i] = algoTotalShares[i];
        }
        return shares;
    }

    function getTimeUntilNextEpoch() external view returns (uint256) {
        uint256 nextEpochTime = lastEpochTime + epochDuration;
        if (block.timestamp >= nextEpochTime) return 0;
        return nextEpochTime - block.timestamp;
    }

    // ============================================================================
    // Admin Functions
    // ============================================================================

    function setOperator(address operator, bool authorized) external onlyOwner {
        operators[operator] = authorized;
        emit OperatorUpdated(operator, authorized);
    }

    function setPoolOperator(address pool, bool authorized) external onlyOwner {
        poolOperators[pool] = authorized;
        emit PoolOperatorUpdated(pool, authorized);
    }

    function setRewardPerShare(uint256 newReward) external onlyOwner {
        emit RewardPerShareUpdated(rewardPerShare, newReward);
        rewardPerShare = newReward;
    }

    function setEpochDuration(uint256 duration) external onlyOwner {
        require(duration >= 1 minutes && duration <= 1 days, "Invalid duration");
        epochDuration = duration;
    }

    // ============================================================================
    // Internal
    // ============================================================================

    function _mint(address to, uint256 amount) internal {
        totalSupply += amount;
        balanceOf[to] += amount;
        emit Transfer(address(0), to, amount);
    }
}
