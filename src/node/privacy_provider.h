// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_NODE_PRIVACY_PROVIDER_H
#define WATTX_NODE_PRIVACY_PROVIDER_H

#include <privacy/ring_signature.h>
#include <coins.h>
#include <txdb.h>
#include <validation.h>
#include <sync.h>

#include <memory>
#include <vector>
#include <random>

class ChainstateManager;
class CBlockIndex;

namespace node {

/**
 * @brief Output index entry for decoy selection
 *
 * Maintains a global index of all outputs for efficient random selection.
 * Stored in a separate LevelDB database.
 */
struct COutputIndexEntry
{
    COutPoint outpoint;
    uint32_t height;
    CAmount amount;
    bool isCoinbase;
    bool isCoinStake;

    SERIALIZE_METHODS(COutputIndexEntry, obj) {
        READWRITE(obj.outpoint, obj.height, obj.amount, obj.isCoinbase, obj.isCoinStake);
    }
};

/**
 * @brief Database for output index (for decoy selection)
 */
class COutputIndexDB
{
private:
    std::unique_ptr<CDBWrapper> m_db;
    mutable RecursiveMutex cs_db;

public:
    explicit COutputIndexDB(const fs::path& path, size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    //! Get total number of indexed outputs
    uint64_t GetOutputCount() const;

    //! Get output by global index
    bool GetOutput(uint64_t globalIndex, COutputIndexEntry& entry) const;

    //! Get the first global index at or after a given height
    bool GetFirstIndexAtHeight(int height, uint64_t& globalIndex) const;

    //! Add outputs from a block
    bool WriteBlock(int height, const std::vector<COutputIndexEntry>& outputs);

    //! Remove outputs (for reorg)
    bool EraseBlock(int height, uint64_t startIndex, uint64_t count);

    //! Get/set best indexed block
    bool GetBestBlock(uint256& hash) const;
    bool SetBestBlock(const uint256& hash);

    //! Sync to disk
    bool Sync();
};

/**
 * @brief Chainstate-based implementation of IDecoyProvider
 *
 * Provides decoy outputs for ring signatures by accessing the UTXO set
 * and output index database.
 */
class ChainstateDecoyProvider : public privacy::IDecoyProvider
{
private:
    ChainstateManager& m_chainman;
    std::shared_ptr<COutputIndexDB> m_outputIndex;
    mutable RecursiveMutex cs_provider;
    mutable std::mt19937_64 m_rng;

public:
    ChainstateDecoyProvider(ChainstateManager& chainman, std::shared_ptr<COutputIndexDB> outputIndex);

    //! IDecoyProvider interface
    uint64_t GetOutputCount() const override;
    int GetHeight() const override;
    bool GetOutputByIndex(uint64_t globalIndex, privacy::CDecoyCandidate& candidate) const override;
    size_t GetRandomOutputs(size_t count, int minHeight, int maxHeight,
                            std::vector<privacy::CDecoyCandidate>& candidates) const override;

    //! Index management
    bool IndexBlock(const CBlock& block, const CBlockIndex* pindex);
    bool UnindexBlock(const CBlockIndex* pindex);
    bool IsSynced() const;

    //! Initialize/rebuild index
    bool Initialize();
    bool RebuildIndex(std::function<void(int, int)> progressCallback = nullptr);
};

/**
 * @brief Initialize the global decoy provider
 *
 * Called during node initialization after chainstate is loaded.
 */
bool InitializeDecoyProvider(ChainstateManager& chainman, const fs::path& datadir);

/**
 * @brief Shutdown the decoy provider
 */
void ShutdownDecoyProvider();

/**
 * @brief Get the global decoy provider (for wallet/RPC use)
 */
std::shared_ptr<ChainstateDecoyProvider> GetChainstateDecoyProvider();

} // namespace node

#endif // WATTX_NODE_PRIVACY_PROVIDER_H
