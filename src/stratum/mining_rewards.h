// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_STRATUM_MINING_REWARDS_H
#define WATTX_STRATUM_MINING_REWARDS_H

#include <uint256.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <queue>
#include <thread>
#include <condition_variable>

namespace mining_rewards {

/**
 * Configuration for mining rewards contract integration
 */
struct MiningRewardsConfig {
    // Contract address on WATTx mainnet
    std::string contract_address;

    // RPC connection to WATTx node
    std::string wattx_rpc_host = "127.0.0.1";
    uint16_t wattx_rpc_port = 1337;
    std::string wattx_rpc_user;
    std::string wattx_rpc_pass;

    // Operator wallet for signing transactions
    std::string operator_address;

    // Batch settings
    int batch_interval_seconds = 30;     // Submit shares every N seconds
    int max_batch_size = 100;            // Max shares per transaction

    // Enable/disable
    bool enabled = false;
};

/**
 * Share submission to be reported to contract
 */
struct ShareSubmission {
    std::string miner_address;           // Miner's WATTx address
    uint64_t shares;                     // Number of shares
    bool xmr_valid;                      // Met Monero target
    bool wtx_valid;                      // Met WATTx target
    uint64_t monero_height;              // Monero block height
    uint64_t wattx_height;               // WATTx block height
    int64_t timestamp;                   // Submission time
};

/**
 * Mining Rewards Manager
 * Integrates merged mining stratum with on-chain rewards contract
 */
class MiningRewardsManager {
public:
    MiningRewardsManager();
    ~MiningRewardsManager();

    /**
     * Initialize with configuration
     */
    bool Initialize(const MiningRewardsConfig& config);

    /**
     * Start the rewards submission thread
     */
    bool Start();

    /**
     * Stop the manager
     */
    void Stop();

    /**
     * Check if running
     */
    bool IsRunning() const { return m_running.load(); }

    /**
     * Queue a share submission for reporting to contract
     */
    void QueueShare(const ShareSubmission& share);

    /**
     * Force submit pending shares immediately
     */
    void FlushPendingShares();

    /**
     * Signal new block found (triggers contract finalization)
     */
    void NotifyBlockFound(uint64_t moneroHeight, uint64_t wattxHeight);

    /**
     * Get pending share count
     */
    size_t GetPendingShareCount() const;

    /**
     * Get statistics
     */
    uint64_t GetTotalSharesSubmitted() const { return m_total_shares_submitted.load(); }
    uint64_t GetTotalTxSent() const { return m_total_tx_sent.load(); }
    uint64_t GetTotalBlocksFinalized() const { return m_total_blocks_finalized.load(); }

    /**
     * Get contract address
     */
    std::string GetContractAddress() const { return m_config.contract_address; }

private:
    // Worker thread
    void SubmissionThread();

    // Submit batch of shares to contract
    bool SubmitSharesBatch(const std::vector<ShareSubmission>& shares);

    // Call finalizeBlock on contract
    bool FinalizeBlock();

    // Build contract call data
    std::string BuildSubmitSharesCalldata(const ShareSubmission& share);
    std::string BuildFinalizeBlockCalldata();

    // Send transaction to contract
    std::string SendContractTransaction(const std::string& calldata, uint64_t gas = 200000);

    // RPC communication
    std::string WattxRPC(const std::string& method, const std::string& params);
    std::string HttpPost(const std::string& host, uint16_t port,
                          const std::string& path, const std::string& body,
                          const std::string& auth = "");

    // Encode address for contract call
    std::string EncodeAddress(const std::string& address);

    // Encode uint256 for contract call
    std::string EncodeUint256(uint64_t value);

    // Encode bool for contract call
    std::string EncodeBool(bool value);

    // Configuration
    MiningRewardsConfig m_config;

    // State
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_initialized{false};

    // Share queue
    mutable std::mutex m_queue_mutex;
    std::queue<ShareSubmission> m_share_queue;

    // Block notification
    std::mutex m_block_mutex;
    std::condition_variable m_block_cv;
    bool m_block_found{false};
    uint64_t m_last_monero_height{0};
    uint64_t m_last_wattx_height{0};

    // Worker thread
    std::thread m_submission_thread;
    std::condition_variable m_cv;
    std::mutex m_cv_mutex;

    // Statistics
    std::atomic<uint64_t> m_total_shares_submitted{0};
    std::atomic<uint64_t> m_total_tx_sent{0};
    std::atomic<uint64_t> m_total_blocks_finalized{0};
};

// Global instance
MiningRewardsManager& GetMiningRewardsManager();

}  // namespace mining_rewards

#endif // WATTX_STRATUM_MINING_REWARDS_H
