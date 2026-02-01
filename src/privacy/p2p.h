// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_PRIVACY_P2P_H
#define WATTX_PRIVACY_P2P_H

#include <privacy/privacy.h>
#include <privacy/consensus.h>
#include <primitives/transaction.h>
#include <validation.h>

#include <memory>
#include <set>

namespace privacy {

/**
 * @brief Result of privacy transaction P2P validation
 */
struct CPrivacyP2PResult
{
    bool isPrivacyTx{false};      // Whether this is a privacy transaction
    bool isValid{false};           // Whether validation passed
    std::string rejectReason;      // Reason if rejected
    std::vector<CKeyImage> keyImages;  // Key images from this tx
};

/**
 * @brief P2P handler for privacy transactions
 *
 * Hooks into the transaction validation pipeline to perform
 * privacy-specific validation and key image tracking.
 */
class CPrivacyP2PHandler
{
public:
    CPrivacyP2PHandler();
    ~CPrivacyP2PHandler();

    /**
     * @brief Pre-validate a transaction for privacy rules
     *
     * Called before mempool acceptance. Checks:
     * - If this is a privacy transaction
     * - Key images not already seen (mempool + chain)
     * - Basic structure validation
     *
     * @param tx The transaction to check
     * @param result [out] Validation result
     * @return true if should continue to standard validation
     */
    bool PreValidateTransaction(const CTransaction& tx, CPrivacyP2PResult& result);

    /**
     * @brief Post-validate after mempool acceptance
     *
     * Called after transaction is accepted to mempool.
     * Tracks key images for double-spend detection.
     *
     * @param tx The accepted transaction
     * @param result The validation result from pre-validation
     */
    void OnTransactionAccepted(const CTransaction& tx, const CPrivacyP2PResult& result);

    /**
     * @brief Handle transaction removal from mempool
     *
     * Called when transaction is removed (confirmed or evicted).
     * Cleans up mempool key image tracking.
     *
     * @param tx The removed transaction
     */
    void OnTransactionRemoved(const CTransaction& tx);

    /**
     * @brief Check if a key image is in mempool
     *
     * @param keyImage The key image to check
     * @return true if key image is in a mempool transaction
     */
    bool IsKeyImageInMempool(const CKeyImage& keyImage) const;

    /**
     * @brief Get all mempool key images
     *
     * @return Set of key image hashes in mempool
     */
    std::set<uint256> GetMempoolKeyImages() const;

    /**
     * @brief Clear mempool key image tracking (e.g., on reorg)
     */
    void ClearMempoolKeyImages();

private:
    mutable RecursiveMutex cs_privacy_p2p;

    // Key images currently in mempool (hash -> txid)
    std::map<uint256, uint256> m_mempoolKeyImages GUARDED_BY(cs_privacy_p2p);

    // Transactions we've seen as privacy transactions (txid -> key image hashes)
    std::map<uint256, std::vector<uint256>> m_privacyTxKeyImages GUARDED_BY(cs_privacy_p2p);
};

/**
 * @brief Get the global P2P handler
 */
CPrivacyP2PHandler& GetPrivacyP2PHandler();

/**
 * @brief Check transaction for privacy rules before mempool
 *
 * Hook function to be called from validation.cpp
 *
 * @param tx Transaction to check
 * @param state [out] Validation state
 * @return true if transaction passes privacy checks (or is not a privacy tx)
 */
bool CheckTransactionPrivacy(const CTransaction& tx, TxValidationState& state);

/**
 * @brief Connect privacy transaction to block
 *
 * Hook function to be called during block connection.
 *
 * @param tx Transaction being connected
 * @param blockHeight Block height
 * @return true if successful
 */
bool ConnectPrivacyTx(const CTransaction& tx, int blockHeight);

/**
 * @brief Disconnect privacy transaction from block
 *
 * Hook function to be called during block disconnection (reorg).
 *
 * @param tx Transaction being disconnected
 * @return true if successful
 */
bool DisconnectPrivacyTx(const CTransaction& tx);

} // namespace privacy

#endif // WATTX_PRIVACY_P2P_H
