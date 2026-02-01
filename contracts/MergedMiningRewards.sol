// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

/**
 * @title MergedMiningRewards
 * @dev Rewards contract for WATTx-Monero merged mining test tokens
 *
 * Miners submit shares during dual mining and receive WXTM (WATTx Test Mining) tokens
 * proportional to their share contributions per block.
 */

import "./IERC20.sol";

contract MergedMiningRewards {
    // ============================================================================
    // Token Information
    // ============================================================================

    string public constant name = "WATTx Test Mining Token";
    string public constant symbol = "WXTM";
    uint8 public constant decimals = 18;

    uint256 public totalSupply;
    mapping(address => uint256) public balanceOf;
    mapping(address => mapping(address => uint256)) public allowance;

    // ============================================================================
    // Mining State
    // ============================================================================

    // Contract owner (can add/remove operators)
    address public owner;

    // Authorized operators (stratum servers that can report shares)
    mapping(address => bool) public operators;

    // Base reward per share (in wei, 1 WXTM = 1e18)
    uint256 public rewardPerShare = 1e15; // 0.001 WXTM per share

    // Bonus multiplier for blocks that hit both XMR and WTX targets (100 = 1x, 150 = 1.5x)
    uint256 public dualMiningBonus = 150; // 1.5x bonus for dual mining

    // Block tracking
    struct BlockInfo {
        uint256 moneroHeight;
        uint256 wattxHeight;
        uint256 totalShares;
        uint256 totalRewards;
        bool finalized;
    }

    // blockId => BlockInfo
    mapping(uint256 => BlockInfo) public blocks;
    uint256 public currentBlockId;

    // Share tracking per block per miner
    // blockId => miner => shares
    mapping(uint256 => mapping(address => uint256)) public minerShares;

    // blockId => miner => claimed
    mapping(uint256 => mapping(address => bool)) public rewardsClaimed;

    // Miner statistics
    struct MinerStats {
        uint256 totalShares;
        uint256 totalRewards;
        uint256 blocksParticipated;
        uint256 lastShareTime;
    }
    mapping(address => MinerStats) public minerStats;

    // Global statistics
    uint256 public totalSharesAllTime;
    uint256 public totalRewardsAllTime;
    uint256 public totalMiners;
    mapping(address => bool) public knownMiners;

    // ============================================================================
    // Events
    // ============================================================================

    event Transfer(address indexed from, address indexed to, uint256 value);
    event Approval(address indexed owner, address indexed spender, uint256 value);

    event ShareSubmitted(
        address indexed miner,
        uint256 indexed blockId,
        uint256 shares,
        bool xmrValid,
        bool wtxValid
    );

    event BlockFinalized(
        uint256 indexed blockId,
        uint256 moneroHeight,
        uint256 wattxHeight,
        uint256 totalShares,
        uint256 totalRewards
    );

    event RewardsClaimed(
        address indexed miner,
        uint256 indexed blockId,
        uint256 amount
    );

    event OperatorAdded(address indexed operator);
    event OperatorRemoved(address indexed operator);
    event RewardPerShareUpdated(uint256 oldValue, uint256 newValue);
    event DualMiningBonusUpdated(uint256 oldValue, uint256 newValue);

    // ============================================================================
    // Modifiers
    // ============================================================================

    modifier onlyOwner() {
        require(msg.sender == owner, "Not owner");
        _;
    }

    modifier onlyOperator() {
        require(operators[msg.sender] || msg.sender == owner, "Not operator");
        _;
    }

    // ============================================================================
    // Constructor
    // ============================================================================

    constructor() {
        owner = msg.sender;
        operators[msg.sender] = true;
        currentBlockId = 1;

        emit OperatorAdded(msg.sender);
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
    // Mining Functions (called by stratum server)
    // ============================================================================

    /**
     * @dev Submit shares for a miner during merged mining
     * @param miner Address of the miner
     * @param shares Number of shares submitted
     * @param xmrValid Whether shares met Monero target
     * @param wtxValid Whether shares met WATTx target
     * @param moneroHeight Current Monero block height
     * @param wattxHeight Current WATTx block height
     */
    function submitShares(
        address miner,
        uint256 shares,
        bool xmrValid,
        bool wtxValid,
        uint256 moneroHeight,
        uint256 wattxHeight
    ) external onlyOperator {
        require(miner != address(0), "Invalid miner address");
        require(shares > 0, "No shares");

        // Initialize block if new
        BlockInfo storage block_ = blocks[currentBlockId];
        if (block_.moneroHeight == 0) {
            block_.moneroHeight = moneroHeight;
            block_.wattxHeight = wattxHeight;
        }

        // Track new miners
        if (!knownMiners[miner]) {
            knownMiners[miner] = true;
            totalMiners++;
        }

        // Calculate effective shares (bonus for dual mining)
        uint256 effectiveShares = shares;
        if (xmrValid && wtxValid) {
            effectiveShares = (shares * dualMiningBonus) / 100;
        }

        // Record shares
        minerShares[currentBlockId][miner] += effectiveShares;
        block_.totalShares += effectiveShares;

        // Update miner stats
        MinerStats storage stats = minerStats[miner];
        stats.totalShares += effectiveShares;
        stats.lastShareTime = block.timestamp;
        if (minerShares[currentBlockId][miner] == effectiveShares) {
            stats.blocksParticipated++;
        }

        // Global stats
        totalSharesAllTime += effectiveShares;

        emit ShareSubmitted(miner, currentBlockId, effectiveShares, xmrValid, wtxValid);
    }

    /**
     * @dev Finalize current block and start new one
     * Called when a new Monero/WATTx block is found
     */
    function finalizeBlock() external onlyOperator {
        BlockInfo storage block_ = blocks[currentBlockId];
        require(!block_.finalized, "Block already finalized");
        require(block_.totalShares > 0, "No shares in block");

        // Calculate total rewards for this block
        block_.totalRewards = block_.totalShares * rewardPerShare;
        block_.finalized = true;

        totalRewardsAllTime += block_.totalRewards;

        emit BlockFinalized(
            currentBlockId,
            block_.moneroHeight,
            block_.wattxHeight,
            block_.totalShares,
            block_.totalRewards
        );

        // Move to next block
        currentBlockId++;
    }

    /**
     * @dev Claim rewards for a specific block
     * @param blockId Block ID to claim rewards for
     */
    function claimRewards(uint256 blockId) external {
        require(blocks[blockId].finalized, "Block not finalized");
        require(!rewardsClaimed[blockId][msg.sender], "Already claimed");
        require(minerShares[blockId][msg.sender] > 0, "No shares in block");

        uint256 shares = minerShares[blockId][msg.sender];
        uint256 reward = shares * rewardPerShare;

        rewardsClaimed[blockId][msg.sender] = true;

        // Mint tokens to miner
        _mint(msg.sender, reward);

        // Update stats
        minerStats[msg.sender].totalRewards += reward;

        emit RewardsClaimed(msg.sender, blockId, reward);
    }

    /**
     * @dev Claim rewards for multiple blocks at once
     * @param blockIds Array of block IDs to claim
     */
    function claimMultipleRewards(uint256[] calldata blockIds) external {
        uint256 totalReward = 0;

        for (uint256 i = 0; i < blockIds.length; i++) {
            uint256 blockId = blockIds[i];

            if (!blocks[blockId].finalized) continue;
            if (rewardsClaimed[blockId][msg.sender]) continue;
            if (minerShares[blockId][msg.sender] == 0) continue;

            uint256 shares = minerShares[blockId][msg.sender];
            uint256 reward = shares * rewardPerShare;

            rewardsClaimed[blockId][msg.sender] = true;
            totalReward += reward;

            emit RewardsClaimed(msg.sender, blockId, reward);
        }

        if (totalReward > 0) {
            _mint(msg.sender, totalReward);
            minerStats[msg.sender].totalRewards += totalReward;
        }
    }

    /**
     * @dev Get pending (unclaimed) rewards for a miner
     * @param miner Address to check
     * @return pending Total pending rewards
     * @return blockCount Number of blocks with pending rewards
     */
    function getPendingRewards(address miner) external view returns (uint256 pending, uint256 blockCount) {
        for (uint256 i = 1; i < currentBlockId; i++) {
            if (blocks[i].finalized &&
                !rewardsClaimed[i][miner] &&
                minerShares[i][miner] > 0) {
                pending += minerShares[i][miner] * rewardPerShare;
                blockCount++;
            }
        }
    }

    /**
     * @dev Get block IDs with pending rewards for a miner
     * @param miner Address to check
     * @param maxBlocks Maximum blocks to return
     */
    function getPendingBlockIds(address miner, uint256 maxBlocks)
        external view returns (uint256[] memory)
    {
        uint256[] memory temp = new uint256[](maxBlocks);
        uint256 count = 0;

        for (uint256 i = 1; i < currentBlockId && count < maxBlocks; i++) {
            if (blocks[i].finalized &&
                !rewardsClaimed[i][miner] &&
                minerShares[i][miner] > 0) {
                temp[count] = i;
                count++;
            }
        }

        // Trim array
        uint256[] memory result = new uint256[](count);
        for (uint256 i = 0; i < count; i++) {
            result[i] = temp[i];
        }
        return result;
    }

    // ============================================================================
    // Admin Functions
    // ============================================================================

    function addOperator(address operator) external onlyOwner {
        require(operator != address(0), "Invalid operator");
        operators[operator] = true;
        emit OperatorAdded(operator);
    }

    function removeOperator(address operator) external onlyOwner {
        require(operator != owner, "Cannot remove owner");
        operators[operator] = false;
        emit OperatorRemoved(operator);
    }

    function setRewardPerShare(uint256 newReward) external onlyOwner {
        emit RewardPerShareUpdated(rewardPerShare, newReward);
        rewardPerShare = newReward;
    }

    function setDualMiningBonus(uint256 newBonus) external onlyOwner {
        require(newBonus >= 100, "Bonus must be >= 100");
        emit DualMiningBonusUpdated(dualMiningBonus, newBonus);
        dualMiningBonus = newBonus;
    }

    function transferOwnership(address newOwner) external onlyOwner {
        require(newOwner != address(0), "Invalid owner");
        owner = newOwner;
    }

    // ============================================================================
    // View Functions
    // ============================================================================

    function getBlockInfo(uint256 blockId) external view returns (
        uint256 moneroHeight,
        uint256 wattxHeight,
        uint256 totalShares,
        uint256 totalRewards,
        bool finalized
    ) {
        BlockInfo storage b = blocks[blockId];
        return (b.moneroHeight, b.wattxHeight, b.totalShares, b.totalRewards, b.finalized);
    }

    function getMinerBlockShares(uint256 blockId, address miner) external view returns (uint256) {
        return minerShares[blockId][miner];
    }

    function getGlobalStats() external view returns (
        uint256 _totalMiners,
        uint256 _totalSharesAllTime,
        uint256 _totalRewardsAllTime,
        uint256 _currentBlockId,
        uint256 _rewardPerShare
    ) {
        return (totalMiners, totalSharesAllTime, totalRewardsAllTime, currentBlockId, rewardPerShare);
    }

    // ============================================================================
    // Internal Functions
    // ============================================================================

    function _mint(address to, uint256 amount) internal {
        totalSupply += amount;
        balanceOf[to] += amount;
        emit Transfer(address(0), to, amount);
    }
}

// Simple interface for ERC20
interface IERC20 {
    function totalSupply() external view returns (uint256);
    function balanceOf(address account) external view returns (uint256);
    function transfer(address to, uint256 amount) external returns (bool);
    function allowance(address owner, address spender) external view returns (uint256);
    function approve(address spender, uint256 amount) external returns (bool);
    function transferFrom(address from, address to, uint256 amount) external returns (bool);
}
