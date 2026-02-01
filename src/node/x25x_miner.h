// Copyright (c) 2024 The WATTx developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_X25X_MINER_H
#define BITCOIN_NODE_X25X_MINER_H

#include <crypto/x25x/x25x.h>
#include <primitives/block.h>
#include <uint256.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace node {

/**
 * X25X Multi-Algorithm Miner
 *
 * Provides unified mining interface for all X25X-supported algorithms.
 * Miners can select their preferred algorithm based on hardware capabilities:
 *
 * - SHA256d:  ASICs (Bitcoin miners)
 * - Scrypt:   ASICs (Litecoin miners), also GPU
 * - Ethash:   GPUs (Ethereum miners)
 * - RandomX:  CPUs (Monero miners)
 * - Equihash: GPUs (ZCash miners)
 * - X11:      GPUs and ASICs (Dash miners)
 */
class X25XMiner {
public:
    using BlockFoundCallback = std::function<void(const CBlock& block)>;

    X25XMiner();
    ~X25XMiner();

    // Disable copy
    X25XMiner(const X25XMiner&) = delete;
    X25XMiner& operator=(const X25XMiner&) = delete;

    /**
     * Initialize the miner for a specific algorithm
     *
     * @param algo The algorithm to use
     * @return true if initialization successful
     */
    bool Initialize(x25x::Algorithm algo);

    /**
     * Set the algorithm to use for mining
     */
    void SetAlgorithm(x25x::Algorithm algo);

    /**
     * Get the currently selected algorithm
     */
    x25x::Algorithm GetAlgorithm() const { return m_algorithm; }

    /**
     * Start mining with the specified block template and target
     *
     * @param block The block to mine
     * @param target The target hash (must be <= this value)
     * @param numThreads Number of mining threads (0 = auto)
     * @param callback Callback when valid block is found
     */
    void StartMining(const CBlock& block, const uint256& target,
                     int numThreads, BlockFoundCallback callback);

    /**
     * Stop all mining threads
     */
    void StopMining();

    /**
     * Check if currently mining
     */
    bool IsMining() const { return m_mining; }

    /**
     * Get current hashrate (hashes per second)
     */
    double GetHashrate() const;

    /**
     * Get total hashes computed since mining started
     */
    uint64_t GetTotalHashes() const { return m_totalHashes; }

    /**
     * Get algorithm-specific hashrate
     */
    double GetHashrateForAlgorithm(x25x::Algorithm algo) const;

    /**
     * Check if a specific algorithm is available on this system
     */
    static bool IsAlgorithmAvailable(x25x::Algorithm algo);

    /**
     * Get recommended algorithm based on system hardware
     */
    static x25x::Algorithm GetRecommendedAlgorithm();

    /**
     * Get list of available algorithms on this system
     */
    static std::vector<x25x::Algorithm> GetAvailableAlgorithms();

private:
    /**
     * Mining thread function
     */
    void MineThread(int threadId, CBlock block, uint256 target,
                    uint32_t startNonce, uint32_t nonceRange,
                    BlockFoundCallback callback);

    /**
     * Algorithm-specific hash function dispatcher
     */
    uint256 ComputeHash(const CBlockHeader& header);

    // Current mining algorithm
    x25x::Algorithm m_algorithm{x25x::Algorithm::SHA256D};

    // Mining state
    std::atomic<bool> m_mining{false};
    std::atomic<bool> m_stopMining{false};
    std::atomic<uint64_t> m_totalHashes{0};

    // Mining threads
    std::vector<std::thread> m_threads;
    mutable std::mutex m_mutex;

    // Timing
    int64_t m_miningStartTime{0};
    mutable std::atomic<double> m_lastHashrate{0.0};

    // Algorithm-specific contexts
    // These are initialized lazily when needed
    struct AlgorithmContext;
    std::unique_ptr<AlgorithmContext> m_context;
};

/**
 * Global X25X miner instance
 */
X25XMiner& GetX25XMiner();

/**
 * Get stratum job for external miners
 */
struct StratumJob {
    std::string jobId;
    std::string prevHash;
    std::string coinbase1;
    std::string coinbase2;
    std::vector<std::string> merkleBranch;
    std::string version;
    std::string nBits;
    std::string nTime;
    bool cleanJobs;
    x25x::Algorithm algorithm;
};

/**
 * Create a stratum job from a block template
 */
StratumJob CreateStratumJob(const CBlock& block, x25x::Algorithm algo);

/**
 * Verify a stratum solution
 */
bool VerifyStratumSolution(const StratumJob& job, const std::string& nonce,
                           const std::string& solution, x25x::Algorithm algo);

} // namespace node

#endif // BITCOIN_NODE_X25X_MINER_H
