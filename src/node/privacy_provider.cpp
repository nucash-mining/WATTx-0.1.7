// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/privacy_provider.h>
#include <chain.h>
#include <chainparams.h>
#include <logging.h>
#include <node/blockstorage.h>
#include <primitives/block.h>
#include <script/script.h>
#include <script/solver.h>
#include <random.h>
#include <util/fs.h>

#include <algorithm>

namespace node {

// Global decoy provider instance
static std::shared_ptr<ChainstateDecoyProvider> g_decoyProvider;
static std::mutex g_decoyProviderMutex;

// DB key prefixes (as uint8_t to avoid serialization issues)
static constexpr uint8_t DB_OUTPUT = 'o';
static constexpr uint8_t DB_COUNT = 'c';
static constexpr uint8_t DB_HEIGHT = 'h';
static constexpr uint8_t DB_BEST_BLOCK = 'B';

//
// COutputIndexDB Implementation
//

COutputIndexDB::COutputIndexDB(const fs::path& path, size_t nCacheSize, bool fMemory, bool fWipe)
{
    m_db = std::make_unique<CDBWrapper>(DBParams{
        .path = path,
        .cache_bytes = nCacheSize,
        .memory_only = fMemory,
        .wipe_data = fWipe
    });
}

uint64_t COutputIndexDB::GetOutputCount() const
{
    LOCK(cs_db);
    uint64_t count = 0;
    m_db->Read(DB_COUNT, count);
    return count;
}

bool COutputIndexDB::GetOutput(uint64_t globalIndex, COutputIndexEntry& entry) const
{
    LOCK(cs_db);
    return m_db->Read(std::make_pair(DB_OUTPUT, globalIndex), entry);
}

bool COutputIndexDB::GetFirstIndexAtHeight(int height, uint64_t& globalIndex) const
{
    LOCK(cs_db);
    return m_db->Read(std::make_pair(DB_HEIGHT, height), globalIndex);
}

bool COutputIndexDB::WriteBlock(int height, const std::vector<COutputIndexEntry>& outputs)
{
    LOCK(cs_db);

    uint64_t currentCount = 0;
    m_db->Read(DB_COUNT, currentCount);
    uint64_t startIndex = currentCount;

    CDBBatch batch(*m_db);

    // Record first index at this height
    batch.Write(std::make_pair(DB_HEIGHT, height), startIndex);

    // Write each output
    for (size_t i = 0; i < outputs.size(); i++) {
        batch.Write(std::make_pair(DB_OUTPUT, startIndex + i), outputs[i]);
    }

    // Update total count
    batch.Write(DB_COUNT, startIndex + outputs.size());

    return m_db->WriteBatch(batch);
}

bool COutputIndexDB::EraseBlock(int height, uint64_t startIndex, uint64_t count)
{
    LOCK(cs_db);

    CDBBatch batch(*m_db);

    // Erase outputs
    for (uint64_t i = 0; i < count; i++) {
        batch.Erase(std::make_pair(DB_OUTPUT, startIndex + i));
    }

    // Erase height index
    batch.Erase(std::make_pair(DB_HEIGHT, height));

    // Update count
    batch.Write(DB_COUNT, startIndex);

    return m_db->WriteBatch(batch);
}

bool COutputIndexDB::GetBestBlock(uint256& hash) const
{
    LOCK(cs_db);
    return m_db->Read(DB_BEST_BLOCK, hash);
}

bool COutputIndexDB::SetBestBlock(const uint256& hash)
{
    LOCK(cs_db);
    return m_db->Write(DB_BEST_BLOCK, hash);
}

bool COutputIndexDB::Sync()
{
    LOCK(cs_db);
    // Use WriteBatch with sync flag for synchronization
    CDBBatch batch(*m_db);
    return m_db->WriteBatch(batch, true);  // fSync = true
}

//
// ChainstateDecoyProvider Implementation
//

ChainstateDecoyProvider::ChainstateDecoyProvider(ChainstateManager& chainman,
                                                   std::shared_ptr<COutputIndexDB> outputIndex)
    : m_chainman(chainman), m_outputIndex(outputIndex)
{
    // Seed RNG
    uint64_t seed;
    GetStrongRandBytes(Span<unsigned char>(reinterpret_cast<unsigned char*>(&seed), sizeof(seed)));
    m_rng.seed(seed);
}

uint64_t ChainstateDecoyProvider::GetOutputCount() const
{
    LOCK(cs_provider);
    return m_outputIndex ? m_outputIndex->GetOutputCount() : 0;
}

int ChainstateDecoyProvider::GetHeight() const
{
    LOCK(cs_main);
    const CBlockIndex* tip = m_chainman.ActiveChain().Tip();
    return tip ? tip->nHeight : 0;
}

bool ChainstateDecoyProvider::GetOutputByIndex(uint64_t globalIndex,
                                                privacy::CDecoyCandidate& candidate) const
{
    LOCK(cs_provider);

    if (!m_outputIndex) return false;

    COutputIndexEntry entry;
    if (!m_outputIndex->GetOutput(globalIndex, entry)) {
        return false;
    }

    // Get the actual UTXO to verify it's unspent and get pubkey
    LOCK(cs_main);
    CCoinsViewCache& view = m_chainman.ActiveChainstate().CoinsTip();

    std::optional<Coin> coin = view.GetCoin(entry.outpoint);
    if (!coin || coin->IsSpent()) {
        return false;  // Output has been spent
    }

    // Extract public key from script
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(coin->out.scriptPubKey, solutions);

    CPubKey pubkey;
    if (type == TxoutType::PUBKEY && solutions.size() >= 1) {
        pubkey = CPubKey(solutions[0]);
    } else if (type == TxoutType::PUBKEYHASH && solutions.size() >= 1) {
        // For P2PKH, we can't directly get the pubkey without the spending tx
        // Return false - this output can't be used as a decoy without pubkey
        return false;
    } else if (type == TxoutType::WITNESS_V0_KEYHASH || type == TxoutType::WITNESS_V1_TAPROOT) {
        // Segwit outputs - need pubkey from witness
        return false;
    } else {
        // Script type not suitable for ring signature
        return false;
    }

    if (!pubkey.IsValid()) {
        return false;
    }

    candidate.outpoint = entry.outpoint;
    candidate.pubKey = pubkey;
    candidate.amount = coin->out.nValue;
    candidate.height = entry.height;
    candidate.globalIndex = globalIndex;

    return true;
}

size_t ChainstateDecoyProvider::GetRandomOutputs(size_t count, int minHeight, int maxHeight,
                                                  std::vector<privacy::CDecoyCandidate>& candidates) const
{
    LOCK(cs_provider);

    if (!m_outputIndex) return 0;

    candidates.clear();
    candidates.reserve(count);

    uint64_t totalOutputs = m_outputIndex->GetOutputCount();
    if (totalOutputs == 0) return 0;

    // Get index range for height bounds
    uint64_t minIndex = 0;
    uint64_t maxIndex = totalOutputs - 1;

    if (minHeight > 0) {
        m_outputIndex->GetFirstIndexAtHeight(minHeight, minIndex);
    }
    if (maxHeight > 0 && maxHeight < GetHeight()) {
        uint64_t nextHeightIndex;
        if (m_outputIndex->GetFirstIndexAtHeight(maxHeight + 1, nextHeightIndex)) {
            maxIndex = nextHeightIndex > 0 ? nextHeightIndex - 1 : 0;
        }
    }

    if (minIndex > maxIndex) return 0;

    // Random selection with retries
    std::uniform_int_distribution<uint64_t> dist(minIndex, maxIndex);
    std::set<uint64_t> selectedIndices;

    size_t attempts = 0;
    const size_t maxAttempts = count * 10;

    while (candidates.size() < count && attempts < maxAttempts) {
        attempts++;

        uint64_t idx = dist(m_rng);
        if (selectedIndices.count(idx)) continue;

        privacy::CDecoyCandidate candidate;
        if (GetOutputByIndex(idx, candidate)) {
            selectedIndices.insert(idx);
            candidates.push_back(candidate);
        }
    }

    return candidates.size();
}

bool ChainstateDecoyProvider::IndexBlock(const CBlock& block, const CBlockIndex* pindex)
{
    LOCK(cs_provider);

    if (!m_outputIndex) return false;

    std::vector<COutputIndexEntry> outputs;

    for (size_t txIdx = 0; txIdx < block.vtx.size(); txIdx++) {
        const CTransaction& tx = *block.vtx[txIdx];
        bool isCoinbase = tx.IsCoinBase();
        bool isCoinStake = tx.IsCoinStake();

        for (size_t outIdx = 0; outIdx < tx.vout.size(); outIdx++) {
            const CTxOut& out = tx.vout[outIdx];

            // Skip OP_RETURN and other unspendable outputs
            if (out.scriptPubKey.IsUnspendable()) continue;

            // Check if this is a suitable output type (P2PK for ring sigs)
            std::vector<std::vector<unsigned char>> solutions;
            TxoutType type = Solver(out.scriptPubKey, solutions);

            // Only index P2PK outputs (have embedded pubkey)
            if (type == TxoutType::PUBKEY) {
                COutputIndexEntry entry;
                entry.outpoint = COutPoint(tx.GetHash(), outIdx);
                entry.height = pindex->nHeight;
                entry.amount = out.nValue;
                entry.isCoinbase = isCoinbase;
                entry.isCoinStake = isCoinStake;
                outputs.push_back(entry);
            }
        }
    }

    if (!outputs.empty()) {
        if (!m_outputIndex->WriteBlock(pindex->nHeight, outputs)) {
            return false;
        }
    }

    return m_outputIndex->SetBestBlock(pindex->GetBlockHash());
}

bool ChainstateDecoyProvider::UnindexBlock(const CBlockIndex* pindex)
{
    LOCK(cs_provider);

    if (!m_outputIndex) return false;

    uint64_t startIndex;
    if (!m_outputIndex->GetFirstIndexAtHeight(pindex->nHeight, startIndex)) {
        return true;  // Nothing to unindex
    }

    uint64_t totalCount = m_outputIndex->GetOutputCount();
    uint64_t count = totalCount - startIndex;

    return m_outputIndex->EraseBlock(pindex->nHeight, startIndex, count);
}

bool ChainstateDecoyProvider::IsSynced() const
{
    LOCK(cs_provider);
    LOCK(cs_main);

    if (!m_outputIndex) return false;

    uint256 indexBest;
    if (!m_outputIndex->GetBestBlock(indexBest)) {
        return false;
    }

    const CBlockIndex* tip = m_chainman.ActiveChain().Tip();
    return tip && indexBest == tip->GetBlockHash();
}

bool ChainstateDecoyProvider::Initialize()
{
    LOCK(cs_provider);

    if (!m_outputIndex) return false;

    uint256 indexBest;
    if (m_outputIndex->GetBestBlock(indexBest)) {
        // Check if index is on active chain
        LOCK(cs_main);
        const CBlockIndex* pindex = m_chainman.m_blockman.LookupBlockIndex(indexBest);
        if (pindex && m_chainman.ActiveChain().Contains(pindex)) {
            LogPrintf("Privacy output index initialized at height %d\n", pindex->nHeight);
            return true;
        }
    }

    // Need to rebuild
    LogPrintf("Privacy output index needs rebuild\n");
    return RebuildIndex();
}

bool ChainstateDecoyProvider::RebuildIndex(std::function<void(int, int)> progressCallback)
{
    LOCK(cs_provider);
    LOCK(cs_main);

    if (!m_outputIndex) return false;

    LogPrintf("Rebuilding privacy output index...\n");

    const CChain& chain = m_chainman.ActiveChain();
    int tipHeight = chain.Height();

    // Process each block
    for (int height = 0; height <= tipHeight; height++) {
        const CBlockIndex* pindex = chain[height];
        if (!pindex) continue;

        CBlock block;
        if (!m_chainman.m_blockman.ReadBlock(block, *pindex)) {
            LogPrintf("Failed to read block %d for privacy index\n", height);
            return false;
        }

        if (!IndexBlock(block, pindex)) {
            LogPrintf("Failed to index block %d for privacy\n", height);
            return false;
        }

        if (progressCallback && height % 1000 == 0) {
            progressCallback(height, tipHeight);
        }
    }

    m_outputIndex->Sync();

    LogPrintf("Privacy output index rebuilt: %lu outputs indexed\n", m_outputIndex->GetOutputCount());
    return true;
}

//
// Global Functions
//

bool InitializeDecoyProvider(ChainstateManager& chainman, const fs::path& datadir)
{
    std::lock_guard<std::mutex> lock(g_decoyProviderMutex);

    fs::path indexPath = datadir / "privacy_index";

    auto outputIndex = std::make_shared<COutputIndexDB>(
        indexPath,
        1 << 20,  // 1MB cache
        false,    // not memory-only
        false     // don't wipe
    );

    g_decoyProvider = std::make_shared<ChainstateDecoyProvider>(chainman, outputIndex);

    if (!g_decoyProvider->Initialize()) {
        LogPrintf("Warning: Failed to initialize privacy decoy provider\n");
        return false;
    }

    // Register with privacy module
    privacy::SetDecoyProvider(g_decoyProvider);

    LogPrintf("Privacy decoy provider initialized\n");
    return true;
}

void ShutdownDecoyProvider()
{
    std::lock_guard<std::mutex> lock(g_decoyProviderMutex);

    privacy::ClearDecoyProvider();
    g_decoyProvider.reset();

    LogPrintf("Privacy decoy provider shutdown\n");
}

std::shared_ptr<ChainstateDecoyProvider> GetChainstateDecoyProvider()
{
    std::lock_guard<std::mutex> lock(g_decoyProviderMutex);
    return g_decoyProvider;
}

} // namespace node
