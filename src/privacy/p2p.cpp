// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <privacy/p2p.h>
#include <logging.h>
#include <hash.h>

namespace privacy {

// Global P2P handler instance
static std::unique_ptr<CPrivacyP2PHandler> g_privacyP2PHandler;
static std::once_flag g_privacyP2PHandlerInit;

CPrivacyP2PHandler::CPrivacyP2PHandler() = default;
CPrivacyP2PHandler::~CPrivacyP2PHandler() = default;

bool CPrivacyP2PHandler::PreValidateTransaction(const CTransaction& tx, CPrivacyP2PResult& result)
{
    result.isPrivacyTx = false;
    result.isValid = true;

    // Check if this is a privacy transaction
    if (!HasPrivacyData(tx)) {
        return true;  // Not a privacy tx, continue normal validation
    }

    result.isPrivacyTx = true;

    // Extract privacy transaction
    auto privTxOpt = ExtractPrivacyTransaction(tx);
    if (!privTxOpt) {
        result.isValid = false;
        result.rejectReason = "malformed-privacy-data";
        return false;
    }

    const CPrivacyTransaction& privTx = *privTxOpt;

    // Check key images
    LOCK(cs_privacy_p2p);

    for (const auto& input : privTx.privacyInputs) {
        if (!input.keyImage.IsValid()) {
            continue;
        }

        uint256 kiHash = input.keyImage.GetHash();
        result.keyImages.push_back(input.keyImage);

        // Check if key image is already in mempool
        if (m_mempoolKeyImages.count(kiHash)) {
            result.isValid = false;
            result.rejectReason = "key-image-in-mempool";
            LogPrintf("Privacy tx rejected: key image already in mempool: %s\n", kiHash.ToString());
            return false;
        }

        // Check if key image is spent on chain
        auto keyImageDB = GetKeyImageDB();
        if (keyImageDB && keyImageDB->IsSpent(input.keyImage)) {
            result.isValid = false;
            result.rejectReason = "key-image-spent";
            LogPrintf("Privacy tx rejected: key image already spent on chain: %s\n", kiHash.ToString());
            return false;
        }
    }

    // Perform contextless validation
    TxValidationState state;
    if (!CheckPrivacyTransaction(privTx, state, 0)) {  // Height 0 for mempool
        result.isValid = false;
        result.rejectReason = state.GetRejectReason();
        return false;
    }

    // Contextual validation (signatures, range proofs)
    auto keyImageDB = GetKeyImageDB();
    if (keyImageDB) {
        if (!ContextualCheckPrivacyTransaction(privTx, *keyImageDB, state, 0)) {
            result.isValid = false;
            result.rejectReason = state.GetRejectReason();
            return false;
        }
    }

    LogDebug(BCLog::PRIVACY, "Privacy transaction pre-validated: %s\n", tx.GetHash().ToString());
    return true;
}

void CPrivacyP2PHandler::OnTransactionAccepted(const CTransaction& tx, const CPrivacyP2PResult& result)
{
    if (!result.isPrivacyTx || !result.isValid) {
        return;
    }

    LOCK(cs_privacy_p2p);

    uint256 txid = tx.GetHash();
    std::vector<uint256> keyImageHashes;

    // Track key images in mempool
    for (const auto& keyImage : result.keyImages) {
        uint256 kiHash = keyImage.GetHash();
        m_mempoolKeyImages[kiHash] = txid;
        keyImageHashes.push_back(kiHash);
    }

    m_privacyTxKeyImages[txid] = keyImageHashes;

    LogDebug(BCLog::PRIVACY, "Privacy transaction accepted to mempool: %s, key images: %d\n",
             txid.ToString(), keyImageHashes.size());
}

void CPrivacyP2PHandler::OnTransactionRemoved(const CTransaction& tx)
{
    LOCK(cs_privacy_p2p);

    uint256 txid = tx.GetHash();
    auto it = m_privacyTxKeyImages.find(txid);
    if (it == m_privacyTxKeyImages.end()) {
        return;  // Not a tracked privacy tx
    }

    // Remove key images from mempool tracking
    for (const uint256& kiHash : it->second) {
        m_mempoolKeyImages.erase(kiHash);
    }

    m_privacyTxKeyImages.erase(it);

    LogDebug(BCLog::PRIVACY, "Privacy transaction removed from mempool: %s\n", txid.ToString());
}

bool CPrivacyP2PHandler::IsKeyImageInMempool(const CKeyImage& keyImage) const
{
    LOCK(cs_privacy_p2p);
    uint256 kiHash = keyImage.GetHash();
    return m_mempoolKeyImages.count(kiHash) > 0;
}

std::set<uint256> CPrivacyP2PHandler::GetMempoolKeyImages() const
{
    LOCK(cs_privacy_p2p);
    std::set<uint256> result;
    for (const auto& [kiHash, txid] : m_mempoolKeyImages) {
        result.insert(kiHash);
    }
    return result;
}

void CPrivacyP2PHandler::ClearMempoolKeyImages()
{
    LOCK(cs_privacy_p2p);
    m_mempoolKeyImages.clear();
    m_privacyTxKeyImages.clear();
    LogDebug(BCLog::PRIVACY, "Cleared mempool key image tracking\n");
}

//
// Global Functions
//

CPrivacyP2PHandler& GetPrivacyP2PHandler()
{
    std::call_once(g_privacyP2PHandlerInit, []() {
        g_privacyP2PHandler = std::make_unique<CPrivacyP2PHandler>();
    });
    return *g_privacyP2PHandler;
}

bool CheckTransactionPrivacy(const CTransaction& tx, TxValidationState& state)
{
    CPrivacyP2PResult result;
    if (!GetPrivacyP2PHandler().PreValidateTransaction(tx, result)) {
        if (!result.isValid) {
            state.Invalid(TxValidationResult::TX_CONSENSUS, result.rejectReason);
            return false;
        }
    }
    return true;
}

bool ConnectPrivacyTx(const CTransaction& tx, int blockHeight)
{
    if (!HasPrivacyData(tx)) {
        return true;  // Not a privacy tx
    }

    auto privTxOpt = ExtractPrivacyTransaction(tx);
    if (!privTxOpt) {
        return false;
    }

    auto keyImageDB = GetKeyImageDB();
    if (!keyImageDB) {
        LogPrintf("Warning: No key image database available for privacy tx connect\n");
        return true;  // Continue without tracking
    }

    // Mark key images as spent
    if (!ConnectPrivacyTransaction(*privTxOpt, *keyImageDB, tx.GetHash(), blockHeight)) {
        LogPrintf("Failed to connect privacy transaction key images\n");
        return false;
    }

    // Remove from mempool tracking
    GetPrivacyP2PHandler().OnTransactionRemoved(tx);

    return true;
}

bool DisconnectPrivacyTx(const CTransaction& tx)
{
    if (!HasPrivacyData(tx)) {
        return true;  // Not a privacy tx
    }

    auto privTxOpt = ExtractPrivacyTransaction(tx);
    if (!privTxOpt) {
        return false;
    }

    auto keyImageDB = GetKeyImageDB();
    if (!keyImageDB) {
        LogPrintf("Warning: No key image database available for privacy tx disconnect\n");
        return true;
    }

    // Unmark key images
    if (!DisconnectPrivacyTransaction(*privTxOpt, *keyImageDB)) {
        LogPrintf("Failed to disconnect privacy transaction key images\n");
        return false;
    }

    return true;
}

} // namespace privacy
