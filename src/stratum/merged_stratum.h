// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_MERGED_STRATUM_H
#define WATTX_MERGED_STRATUM_H

#include <anchor/evm_anchor.h>
#include <auxpow/auxpow.h>
#include <stratum/mining_rewards.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace interfaces {
class Mining;
class BlockTemplate;
}

namespace merged_stratum {

/**
 * Configuration for the merged mining stratum server
 */
struct MergedStratumConfig {
    // Network settings
    std::string bind_address = "0.0.0.0";
    uint16_t port = 3337;
    int max_clients = 1000;

    // Monero node connection
    std::string monero_daemon_host = "127.0.0.1";
    uint16_t monero_daemon_port = 18081;
    std::string monero_wallet_address;

    // WATTx settings
    std::string wattx_wallet_address;

    // Pool settings
    int job_timeout_seconds = 60;
    uint64_t share_difficulty = 10000;  // Easy share target for tracking
    double pool_fee_percent = 1.0;
};

/**
 * Parsed Monero coinbase data for AuxPoW proof construction
 */
struct MoneroCoinbaseData {
    std::vector<uint8_t> coinbase_tx;        // Full serialized coinbase transaction
    std::vector<uint256> merkle_branch;       // Merkle path from coinbase to block root
    int coinbase_index{0};                    // Index in block (always 0 for coinbase)
    uint256 tx_merkle_root;                   // Transaction merkle root

    // Parsed block header components (from blob)
    uint8_t major_version{0};
    uint8_t minor_version{0};
    uint64_t timestamp{0};
    uint256 prev_hash;
    uint32_t nonce{0};

    // Reserve offset for merge mining tag injection
    size_t reserve_offset{0};                 // Offset in coinbase for extra nonce/tag
    size_t reserve_size{0};                   // Reserved size in coinbase

    bool IsValid() const { return !coinbase_tx.empty(); }
};

/**
 * Merged mining job containing templates for both chains
 */
struct MergedJob {
    std::string job_id;

    // Monero template
    std::string monero_blob;           // Mining blob (76 bytes hex)
    std::string monero_seed_hash;      // RandomX seed
    uint64_t monero_height{0};
    uint64_t monero_difficulty{0};
    uint256 monero_target;

    // Parsed Monero coinbase data for AuxPoW construction
    MoneroCoinbaseData monero_coinbase;
    std::string monero_blocktemplate_blob;   // Full block template (for submission)

    // WATTx template
    std::string wattx_blob;            // WATTx mining blob
    uint64_t wattx_height{0};
    uint32_t wattx_bits{0};
    uint256 wattx_target;
    std::shared_ptr<interfaces::BlockTemplate> wattx_template;

    // Merged mining data
    uint256 aux_merkle_root;           // WATTx commitment in Monero coinbase
    std::vector<uint8_t> merge_mining_tag;

    // EVM transaction anchor (for view key verification)
    evm_anchor::EVMAnchorData evm_anchor;
    std::vector<uint8_t> evm_anchor_tag;  // Serialized anchor for Monero extra

    int64_t created_at{0};
};

/**
 * Connected miner client
 */
struct MergedClient {
    int socket_fd;
    std::string session_id;
    std::string worker_name;
    std::string xmr_address;
    std::string wtx_address;

    bool authorized;
    bool subscribed;

    // Statistics
    uint64_t xmr_shares_accepted;
    uint64_t wtx_shares_accepted;
    uint64_t shares_rejected;
    uint64_t xmr_blocks_found;
    uint64_t wtx_blocks_found;

    int64_t connect_time;
    int64_t last_activity;
    std::string recv_buffer;

    MergedClient()
        : socket_fd(-1), authorized(false), subscribed(false),
          xmr_shares_accepted(0), wtx_shares_accepted(0), shares_rejected(0),
          xmr_blocks_found(0), wtx_blocks_found(0),
          connect_time(0), last_activity(0) {}
};

/**
 * Merged Mining Stratum Server
 *
 * This server provides mining jobs that can validate on both Monero and WATTx.
 * Miners earn dual rewards when their shares meet either chain's target.
 */
class MergedStratumServer {
public:
    MergedStratumServer();
    ~MergedStratumServer();

    /**
     * Start the merged mining stratum server
     */
    bool Start(const MergedStratumConfig& config, interfaces::Mining* wattxMining);

    /**
     * Stop the server
     */
    void Stop();

    /**
     * Check if server is running
     */
    bool IsRunning() const { return m_running.load(); }

    /**
     * Get statistics
     */
    size_t GetClientCount() const;
    uint64_t GetTotalXmrShares() const { return m_total_xmr_shares.load(); }
    uint64_t GetTotalWtxShares() const { return m_total_wtx_shares.load(); }
    uint64_t GetXmrBlocksFound() const { return m_xmr_blocks_found.load(); }
    uint64_t GetWtxBlocksFound() const { return m_wtx_blocks_found.load(); }

    /**
     * Notify of new block on either chain
     */
    void NotifyNewMoneroBlock();
    void NotifyNewWattxBlock();

private:
    // Server threads
    void AcceptThread();
    void ClientThread(int client_id);
    void JobThread();
    void MoneroPollerThread();

    // Protocol handlers
    void HandleMessage(int client_id, const std::string& message);
    void HandleLogin(int client_id, const std::string& id,
                     const std::vector<std::string>& params);
    void HandleSubmit(int client_id, const std::string& id,
                      const std::vector<std::string>& params);
    void HandleGetJob(int client_id, const std::string& id);

    // Job management
    void CreateMergedJob();
    void BroadcastJob(const MergedJob& job);
    bool ValidateShare(int client_id, const std::string& job_id,
                       const std::string& nonce, const std::string& result);

    // Monero daemon communication
    bool GetMoneroBlockTemplate(std::string& blob, std::string& seed_hash,
                                 uint64_t& height, uint64_t& difficulty);
    bool GetMoneroBlockTemplateExtended(MergedJob& job);
    bool SubmitMoneroBlock(const std::string& blob);

    // AuxPoW construction and submission
    bool ConstructAndSubmitAuxPowBlock(int client_id, const MergedJob& job,
                                        const std::string& nonce, const std::string& result);
    bool ParseMoneroBlockBlob(const std::string& blob_hex, MoneroCoinbaseData& coinbase_data);
    CMoneroBlockHeader BuildMoneroHeader(const MoneroCoinbaseData& coinbase_data, uint32_t nonce);

    // Network helpers
    void SendToClient(int client_id, const std::string& message);
    void SendResult(int client_id, const std::string& id, const std::string& result);
    void SendError(int client_id, const std::string& id, int code, const std::string& msg);
    void SendJob(int client_id, const MergedJob& job);
    void DisconnectClient(int client_id);

    // Utility
    std::string GenerateJobId();
    std::string GenerateSessionId();
    std::string HttpPost(const std::string& host, uint16_t port,
                          const std::string& path, const std::string& body);

    // Configuration
    MergedStratumConfig m_config;
    interfaces::Mining* m_wattx_mining{nullptr};

    // Server state
    std::atomic<bool> m_running{false};
    int m_listen_socket{-1};

    // Threads
    std::thread m_accept_thread;
    std::thread m_job_thread;
    std::thread m_monero_poller_thread;
    std::vector<std::thread> m_client_threads;

    // Clients
    mutable std::mutex m_clients_mutex;
    std::unordered_map<int, std::unique_ptr<MergedClient>> m_clients;
    int m_next_client_id{0};

    // Jobs
    mutable std::mutex m_jobs_mutex;
    std::unordered_map<std::string, MergedJob> m_jobs;
    MergedJob m_current_job;
    std::atomic<uint64_t> m_job_counter{0};

    // Current Monero state
    std::mutex m_monero_mutex;
    std::string m_monero_blob;
    std::string m_monero_seed_hash;
    uint64_t m_monero_height{0};
    uint64_t m_monero_difficulty{0};

    // Statistics
    std::atomic<uint64_t> m_total_xmr_shares{0};
    std::atomic<uint64_t> m_total_wtx_shares{0};
    std::atomic<uint64_t> m_xmr_blocks_found{0};
    std::atomic<uint64_t> m_wtx_blocks_found{0};

    // Synchronization
    std::condition_variable m_job_cv;
    std::mutex m_job_cv_mutex;
};

/**
 * Global merged stratum server instance
 */
MergedStratumServer& GetMergedStratumServer();

}  // namespace merged_stratum

#endif  // WATTX_MERGED_STRATUM_H
