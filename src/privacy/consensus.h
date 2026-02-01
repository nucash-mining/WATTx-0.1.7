// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_PRIVACY_CONSENSUS_H
#define WATTX_PRIVACY_CONSENSUS_H

#include <privacy/privacy.h>
#include <consensus/validation.h>
#include <consensus/params.h>
#include <dbwrapper.h>
#include <sync.h>
#include <uint256.h>

#include <memory>
#include <optional>

namespace privacy {

/**
 * @brief Check if privacy transactions are active at given height
 */
bool IsPrivacyActive(int nHeight, const Consensus::Params& params);

/**
 * @brief Key image database entry
 */
struct CKeyImageEntry
{
    uint256 txHash;     // Transaction that spent this key image
    int blockHeight;    // Block height (-1 for mempool)

    SERIALIZE_METHODS(CKeyImageEntry, obj) {
        READWRITE(obj.txHash, obj.blockHeight);
    }
};

/**
 * @brief Persistent database for spent key images
 *
 * Tracks which key images have been used to prevent double-spending
 * of privacy transaction inputs.
 */
class CKeyImageDB
{
private:
    std::unique_ptr<CDBWrapper> m_db;
    mutable RecursiveMutex cs_db;

public:
    explicit CKeyImageDB(const fs::path& path, size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    //! Check if a key image has been spent
    bool IsSpent(const CKeyImage& keyImage) const;

    //! Get the entry for a spent key image
    bool GetEntry(const CKeyImage& keyImage, CKeyImageEntry& entry) const;

    //! Mark a key image as spent
    bool MarkSpent(const CKeyImage& keyImage, const uint256& txHash, int blockHeight);

    //! Unmark a key image (for reorg)
    bool UnmarkSpent(const CKeyImage& keyImage);

    //! Batch operations for block connect/disconnect
    bool WriteKeyImages(const std::vector<std::pair<CKeyImage, CKeyImageEntry>>& entries);
    bool EraseKeyImages(const std::vector<CKeyImage>& keyImages);

    //! Sync to disk
    bool Sync();
};

/**
 * @brief Validation result for privacy transactions
 */
enum class PrivacyValidationResult
{
    VALID,
    INVALID_KEY_IMAGE_SPENT,      // Key image already used
    INVALID_KEY_IMAGE_FORMAT,     // Malformed key image
    INVALID_RING_SIZE,            // Ring too small or too large
    INVALID_RING_SIGNATURE,       // Ring signature verification failed
    INVALID_MLSAG_SIGNATURE,      // MLSAG signature verification failed
    INVALID_COMMITMENT_BALANCE,   // Commitments don't balance
    INVALID_RANGE_PROOF,          // Range proof verification failed
    INVALID_STEALTH_OUTPUT,       // Malformed stealth output
    INVALID_DECOY_SELECTION,      // Invalid decoy outputs in ring
    INVALID_MIXED_TYPES,          // Mixed privacy types not allowed
    ERROR_INTERNAL,               // Internal error during validation
};

/**
 * @brief Convert validation result to string
 */
std::string PrivacyValidationResultToString(PrivacyValidationResult result);

/**
 * @brief Check if a privacy transaction is valid (contextless)
 *
 * Performs validation that doesn't require access to the UTXO set:
 * - Key image format
 * - Ring size bounds
 * - Signature structure
 *
 * @param tx The privacy transaction
 * @param state [out] Validation state
 * @param height Current chain height (for min ring size)
 * @return true if valid
 */
bool CheckPrivacyTransaction(
    const CPrivacyTransaction& tx,
    TxValidationState& state,
    int height);

/**
 * @brief Contextual validation of privacy transaction
 *
 * Performs validation requiring UTXO/keyimage access:
 * - Key image not spent
 * - Ring members exist and are unspent
 * - Ring signature valid
 * - Commitment balance
 * - Range proofs
 *
 * @param tx The privacy transaction
 * @param keyImageDB Key image database
 * @param state [out] Validation state
 * @param height Current chain height
 * @return true if valid
 */
bool ContextualCheckPrivacyTransaction(
    const CPrivacyTransaction& tx,
    const CKeyImageDB& keyImageDB,
    TxValidationState& state,
    int height);

/**
 * @brief Verify a key image is not spent
 */
bool CheckKeyImageNotSpent(
    const CKeyImage& keyImage,
    const CKeyImageDB& keyImageDB,
    TxValidationState& state);

/**
 * @brief Verify ring size is within bounds
 */
bool CheckRingSize(
    const CRing& ring,
    int height,
    TxValidationState& state);

/**
 * @brief Verify all ring members exist and are suitable decoys
 */
bool CheckRingMembers(
    const CRing& ring,
    TxValidationState& state);

/**
 * @brief Connect privacy transaction to a block
 *
 * Marks all key images as spent at the given height.
 *
 * @param tx The privacy transaction
 * @param keyImageDB Key image database
 * @param txHash Transaction hash
 * @param blockHeight Block height
 * @return true if successful
 */
bool ConnectPrivacyTransaction(
    const CPrivacyTransaction& tx,
    CKeyImageDB& keyImageDB,
    const uint256& txHash,
    int blockHeight);

/**
 * @brief Disconnect privacy transaction from a block
 *
 * Unmarks all key images from the transaction.
 *
 * @param tx The privacy transaction
 * @param keyImageDB Key image database
 * @return true if successful
 */
bool DisconnectPrivacyTransaction(
    const CPrivacyTransaction& tx,
    CKeyImageDB& keyImageDB);

/**
 * @brief Initialize the global key image database
 */
bool InitializeKeyImageDB(const fs::path& datadir);

/**
 * @brief Shutdown the key image database
 */
void ShutdownKeyImageDB();

/**
 * @brief Get the global key image database
 */
std::shared_ptr<CKeyImageDB> GetKeyImageDB();

/**
 * @brief Check if a parsed privacy transaction exists in a standard transaction
 *
 * Privacy data is encoded in special OP_RETURN outputs or witness data.
 *
 * @param tx Standard transaction
 * @return true if privacy markers present
 */
bool HasPrivacyData(const CTransaction& tx);

/**
 * @brief Extract privacy transaction from standard transaction
 *
 * @param tx Standard transaction
 * @return Privacy transaction if valid, nullopt otherwise
 */
std::optional<CPrivacyTransaction> ExtractPrivacyTransaction(const CTransaction& tx);

} // namespace privacy

#endif // WATTX_PRIVACY_CONSENSUS_H
