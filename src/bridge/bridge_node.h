// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_BRIDGE_NODE_H
#define WATTX_BRIDGE_NODE_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <uint256.h>
#include <compat/endian.h>
#include <wallet/monero_wallet.h>

// Custom hash specialization for uint256 to use in std::unordered_map
namespace std {
template<>
struct hash<uint256> {
    size_t operator()(const uint256& h) const {
        // Use first 8 bytes as hash (uint256 is already a hash)
        return ReadLE64(h.data());
    }
};
}  // namespace std

namespace bridge {

/**
 * Configuration for the bridge node
 */
struct BridgeConfig {
    // WATTx connection
    std::string wattx_rpc_host = "127.0.0.1";
    uint16_t wattx_rpc_port = 18332;
    std::string wattx_rpc_user;
    std::string wattx_rpc_pass;

    // Monero connection
    std::string monero_daemon_host = "127.0.0.1";
    uint16_t monero_daemon_port = 18081;
    std::string monero_wallet_host = "127.0.0.1";
    uint16_t monero_wallet_port = 18083;

    // Bridge contract address
    std::string bridge_contract_address;
    std::string atomic_swap_address;

    // Validator settings
    bool is_validator = false;
    std::string validator_private_key;

    // Operational settings
    int batch_interval = 600;           // 10 minutes
    int confirmation_threshold = 6;     // Monero confirmations
    int wattx_confirmations = 10;       // WATTx confirmations
};

/**
 * Pending cross-chain transaction
 */
struct PendingTransaction {
    uint256 tx_hash;
    std::string from_chain;     // "wattx" or "monero"
    std::string to_chain;
    uint64_t amount;
    std::string destination;    // Address on destination chain
    int64_t created_at;
    int64_t confirmed_at;
    int confirmations;
    bool completed;
    bool refunded;
};

/**
 * Transaction batch for commitment
 */
struct TransactionBatch {
    uint64_t batch_id;
    std::vector<uint256> tx_hashes;
    uint256 merkle_root;
    int64_t created_at;
    int64_t committed_at;
    bool committed_to_wattx;
    bool confirmed_on_monero;
    std::string monero_block_hash;
    uint64_t monero_height;
};

/**
 * Atomic swap state
 */
struct AtomicSwap {
    uint256 swap_id;
    std::string initiator;          // Address that created the swap
    std::string participant;        // Address that will claim
    uint64_t amount;
    uint256 hash_lock;              // SHA256 hash of secret
    uint256 preimage;               // Secret (revealed on claim)
    int64_t timelock;               // Expiration timestamp
    std::string state;              // "active", "claimed", "refunded"
    bool wattx_side_complete;
    bool monero_side_complete;
};

/**
 * Bridge Node Daemon
 *
 * Coordinates cross-chain operations between WATTx and Monero:
 * - Monitors both chains for relevant transactions
 * - Batches WATTx transactions for Monero commitment
 * - Validates and confirms cross-chain proofs
 * - Facilitates atomic swaps via HTLC coordination
 */
class BridgeNode {
public:
    BridgeNode();
    ~BridgeNode();

    /**
     * Start the bridge node
     */
    bool Start(const BridgeConfig& config);

    /**
     * Stop the bridge node
     */
    void Stop();

    /**
     * Check if bridge is running
     */
    bool IsRunning() const { return m_running.load(); }

    // ============================================================================
    // Transaction Management
    // ============================================================================

    /**
     * Submit a cross-chain transaction
     */
    uint256 SubmitTransaction(const std::string& from_chain,
                               const std::string& to_chain,
                               uint64_t amount,
                               const std::string& destination);

    /**
     * Get transaction status
     */
    PendingTransaction GetTransaction(const uint256& tx_hash);

    /**
     * Get all pending transactions
     */
    std::vector<PendingTransaction> GetPendingTransactions();

    // ============================================================================
    // Atomic Swap Management
    // ============================================================================

    /**
     * Initiate an atomic swap (WTX -> XMR)
     */
    uint256 InitiateSwap(uint64_t wtx_amount, const std::string& xmr_destination);

    /**
     * Participate in an atomic swap (XMR -> WTX)
     */
    bool ParticipateSwap(const uint256& swap_id, uint64_t xmr_amount);

    /**
     * Claim a swap (reveal secret)
     */
    bool ClaimSwap(const uint256& swap_id, const uint256& preimage);

    /**
     * Refund an expired swap
     */
    bool RefundSwap(const uint256& swap_id);

    /**
     * Get swap status
     */
    AtomicSwap GetSwap(const uint256& swap_id);

    // ============================================================================
    // Batch Management
    // ============================================================================

    /**
     * Get current batch info
     */
    TransactionBatch GetCurrentBatch();

    /**
     * Force commit current batch (validators only)
     */
    bool CommitBatch();

    // ============================================================================
    // Statistics
    // ============================================================================

    uint64_t GetTotalTransactions() const { return m_total_transactions.load(); }
    uint64_t GetTotalSwaps() const { return m_total_swaps.load(); }
    uint64_t GetPendingCount() const;

private:
    // Worker threads
    void WattxMonitorThread();
    void MoneroMonitorThread();
    void BatchProcessorThread();
    void SwapMonitorThread();

    // Chain interaction
    std::string WattxRPC(const std::string& method, const std::string& params);
    std::string MoneroRPC(const std::string& method, const std::string& params);
    std::string MoneroWalletRPC(const std::string& method, const std::string& params);

    // Transaction handling
    void ProcessWattxBlock(uint64_t height);
    void ProcessMoneroBlock(uint64_t height);
    void UpdateTransactionConfirmations();

    // Batch handling
    void CreateBatch();
    void SubmitBatchToWattx();
    void ConfirmBatchOnMonero();
    uint256 ComputeMerkleRoot(const std::vector<uint256>& hashes);

    // Swap handling
    void MonitorSwapTimeouts();
    bool CreateWattxHTLC(const AtomicSwap& swap);
    bool CreateMoneroHTLC(const AtomicSwap& swap);

    // Utility
    std::string HttpPost(const std::string& host, uint16_t port,
                          const std::string& path, const std::string& body,
                          const std::string& auth = "");

    // Configuration
    BridgeConfig m_config;

    // State
    std::atomic<bool> m_running{false};

    // Threads
    std::thread m_wattx_monitor_thread;
    std::thread m_monero_monitor_thread;
    std::thread m_batch_processor_thread;
    std::thread m_swap_monitor_thread;

    // Chain state
    uint64_t m_wattx_height{0};
    uint64_t m_monero_height{0};

    // Pending transactions
    mutable std::mutex m_tx_mutex;
    std::unordered_map<uint256, PendingTransaction, std::hash<uint256>> m_pending_txs;

    // Batch management
    mutable std::mutex m_batch_mutex;
    TransactionBatch m_current_batch;
    std::vector<TransactionBatch> m_committed_batches;

    // Atomic swaps
    mutable std::mutex m_swap_mutex;
    std::unordered_map<uint256, AtomicSwap, std::hash<uint256>> m_swaps;

    // Statistics
    std::atomic<uint64_t> m_total_transactions{0};
    std::atomic<uint64_t> m_total_swaps{0};

    // Synchronization
    std::condition_variable m_cv;
    std::mutex m_cv_mutex;
};

/**
 * Get global bridge node instance
 */
BridgeNode& GetBridgeNode();

}  // namespace bridge

#endif  // WATTX_BRIDGE_NODE_H
