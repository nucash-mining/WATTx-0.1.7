// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_PRIVACY_RING_SIGNATURE_H
#define WATTX_PRIVACY_RING_SIGNATURE_H

#include <key.h>
#include <pubkey.h>
#include <uint256.h>
#include <serialize.h>
#include <primitives/transaction.h>

#include <vector>
#include <optional>

namespace privacy {

/**
 * Ring Signature Implementation (Borromean/MLSAG-style)
 *
 * Allows a signer to prove they own one of N public keys without
 * revealing which one. Combined with key images to prevent double-spending.
 *
 * Key Image: I = x * Hp(P) where x is private key, P is public key
 * - Unique per key, prevents same key being used twice
 * - Cannot be linked back to the public key
 *
 * Ring: Set of public keys where one belongs to the signer
 * - Decoys (mixins) are other outputs from the blockchain
 * - Larger ring = more privacy, but larger signature size
 */

/**
 * @brief Key image - unique identifier for a spent output
 *
 * Used to detect double-spends without revealing which ring member spent.
 * I = x * Hp(P) where x is secret key, P is public key, Hp is hash-to-point
 */
class CKeyImage
{
public:
    std::vector<unsigned char> data; // 33 bytes compressed point

    CKeyImage() : data(33, 0) {}
    CKeyImage(const std::vector<unsigned char>& d) : data(d) {}

    bool IsValid() const { return data.size() == 33 && data[0] != 0; }
    bool IsNull() const { return data.size() != 33 || data[0] == 0; }

    uint256 GetHash() const;

    bool operator==(const CKeyImage& other) const { return data == other.data; }
    bool operator!=(const CKeyImage& other) const { return !(*this == other); }
    bool operator<(const CKeyImage& other) const { return data < other.data; }

    SERIALIZE_METHODS(CKeyImage, obj) {
        READWRITE(obj.data);
    }
};

/**
 * @brief Ring member - a potential signer in the ring
 */
struct CRingMember
{
    // Reference to the output
    COutPoint outpoint;

    // Public key of this output
    CPubKey pubKey;

    // Commitment (for RingCT)
    std::vector<unsigned char> commitment; // 33 bytes

    CRingMember() = default;
    CRingMember(const COutPoint& out, const CPubKey& pk)
        : outpoint(out), pubKey(pk) {}

    SERIALIZE_METHODS(CRingMember, obj) {
        READWRITE(obj.outpoint, obj.pubKey, obj.commitment);
    }
};

/**
 * @brief A ring of public keys used in the signature
 */
struct CRing
{
    std::vector<CRingMember> members;

    // Index of the real signer (known only to signer, not stored)
    // Set to -1 in serialized form

    size_t Size() const { return members.size(); }
    bool IsValid() const { return members.size() >= 2; }

    SERIALIZE_METHODS(CRing, obj) {
        READWRITE(obj.members);
    }
};

/**
 * @brief Ring signature proving ownership of one member
 */
class CRingSignature
{
public:
    // The ring of public keys
    CRing ring;

    // Key image for double-spend detection
    CKeyImage keyImage;

    // Signature components
    uint256 c0;                          // Initial challenge
    std::vector<uint256> s;              // Responses (one per ring member)

    CRingSignature() = default;

    bool IsValid() const {
        return ring.IsValid() &&
               keyImage.IsValid() &&
               !c0.IsNull() &&
               s.size() == ring.Size();
    }

    SERIALIZE_METHODS(CRingSignature, obj) {
        READWRITE(obj.ring, obj.keyImage, obj.c0, obj.s);
    }
};

/**
 * @brief MLSAG signature for multiple inputs (Multi-Layered Linkable Spontaneous Anonymous Group)
 */
class CMLSAGSignature
{
public:
    // Multiple rings (one per input)
    std::vector<CRing> rings;

    // Key images (one per input)
    std::vector<CKeyImage> keyImages;

    // Signature data
    uint256 c0;                              // Initial challenge
    std::vector<std::vector<uint256>> s;     // Responses [input][ring_member]

    CMLSAGSignature() = default;

    bool IsValid() const {
        if (rings.empty() || keyImages.size() != rings.size()) return false;
        if (s.size() != rings.size()) return false;
        for (size_t i = 0; i < rings.size(); i++) {
            if (!rings[i].IsValid() || !keyImages[i].IsValid()) return false;
            if (s[i].size() != rings[i].Size()) return false;
        }
        return !c0.IsNull();
    }

    size_t InputCount() const { return rings.size(); }
    size_t RingSize() const { return rings.empty() ? 0 : rings[0].Size(); }

    SERIALIZE_METHODS(CMLSAGSignature, obj) {
        READWRITE(obj.rings, obj.keyImages, obj.c0, obj.s);
    }
};

/**
 * @brief Generate key image for a public key
 *
 * @param privKey The private key
 * @param pubKey The corresponding public key
 * @param keyImage [out] The generated key image
 * @return true if generation succeeded
 */
bool GenerateKeyImage(
    const CKey& privKey,
    const CPubKey& pubKey,
    CKeyImage& keyImage);

/**
 * @brief Create a ring signature
 *
 * @param message The message being signed (typically transaction hash)
 * @param ring The ring of public keys
 * @param realIndex Index of the real signer in the ring
 * @param privKey Private key of the real signer
 * @param sig [out] The generated signature
 * @return true if signing succeeded
 */
bool CreateRingSignature(
    const uint256& message,
    const CRing& ring,
    size_t realIndex,
    const CKey& privKey,
    CRingSignature& sig);

/**
 * @brief Verify a ring signature
 *
 * @param message The message that was signed
 * @param sig The signature to verify
 * @return true if signature is valid
 */
bool VerifyRingSignature(
    const uint256& message,
    const CRingSignature& sig);

/**
 * @brief Create an MLSAG signature for multiple inputs
 *
 * @param message The message being signed
 * @param rings Vector of rings (one per input)
 * @param realIndices Index of real signer in each ring
 * @param privKeys Private keys for each input
 * @param sig [out] The generated signature
 * @return true if signing succeeded
 */
bool CreateMLSAGSignature(
    const uint256& message,
    const std::vector<CRing>& rings,
    const std::vector<size_t>& realIndices,
    const std::vector<CKey>& privKeys,
    CMLSAGSignature& sig);

/**
 * @brief Verify an MLSAG signature
 *
 * @param message The message that was signed
 * @param sig The signature to verify
 * @return true if signature is valid
 */
bool VerifyMLSAGSignature(
    const uint256& message,
    const CMLSAGSignature& sig);

/**
 * @brief Hash a point to the curve (for key image generation)
 *
 * @param pubKey Input point
 * @param result [out] Resulting point on curve
 * @return true if hash succeeded
 */
bool HashToPoint(
    const CPubKey& pubKey,
    CPubKey& result);

/**
 * @brief UTXO information for decoy selection
 */
struct CDecoyCandidate
{
    COutPoint outpoint;
    CPubKey pubKey;
    CAmount amount;
    uint32_t height;
    uint64_t globalIndex;  // Position in total output set
};

/**
 * @brief Decoy selection criteria
 */
struct CDecoySelectionParams
{
    // Minimum confirmations for decoy
    int minConfirmations{10};

    // Maximum confirmations (0 = no limit)
    int maxConfirmations{0};

    // Amount similarity (0.0 = exact, 1.0 = any amount)
    double amountSimilarity{0.5};

    // Prefer recent outputs (gamma distribution like Monero)
    bool useGammaDistribution{true};

    // Gamma shape parameter (Monero uses ~19.28)
    double gammaShape{19.28};

    // Whether to exclude coinbase/coinstake outputs
    bool excludeCoinbaseStake{true};
};

/**
 * @brief Interface for UTXO set access (for decoy selection)
 *
 * This interface allows the privacy module to access UTXOs without
 * directly depending on the chainstate. Implementations should be
 * thread-safe.
 */
class IDecoyProvider
{
public:
    virtual ~IDecoyProvider() = default;

    /**
     * @brief Get total number of outputs in the chain
     */
    virtual uint64_t GetOutputCount() const = 0;

    /**
     * @brief Get current chain height
     */
    virtual int GetHeight() const = 0;

    /**
     * @brief Get output at a specific global index
     * @param globalIndex Index in the output set
     * @param candidate [out] The output information
     * @return true if output exists and is unspent
     */
    virtual bool GetOutputByIndex(uint64_t globalIndex, CDecoyCandidate& candidate) const = 0;

    /**
     * @brief Get multiple random outputs
     * @param count Number of outputs to get
     * @param minHeight Minimum block height
     * @param maxHeight Maximum block height
     * @param candidates [out] Vector of candidates
     * @return Number of candidates actually retrieved
     */
    virtual size_t GetRandomOutputs(
        size_t count,
        int minHeight,
        int maxHeight,
        std::vector<CDecoyCandidate>& candidates) const = 0;
};

/**
 * @brief Set the global decoy provider
 *
 * Called during node initialization to set up UTXO access.
 * Thread-safe; provider must remain valid until ClearDecoyProvider is called.
 */
void SetDecoyProvider(std::shared_ptr<IDecoyProvider> provider);

/**
 * @brief Clear the global decoy provider
 */
void ClearDecoyProvider();

/**
 * @brief Get the current decoy provider
 */
std::shared_ptr<IDecoyProvider> GetDecoyProvider();

/**
 * @brief Select decoy outputs for a ring
 *
 * Uses the registered IDecoyProvider to select realistic decoys.
 * Selection follows gamma distribution (like Monero) to mimic
 * real spending patterns.
 *
 * @param realOutput The real output to hide
 * @param ringSize Desired ring size (includes real output)
 * @param realAmount Amount of the real output (for similarity matching)
 * @param realPubKey Public key of the real output
 * @param params Selection parameters
 * @param decoys [out] Selected decoy outputs
 * @return true if selection succeeded (enough decoys found)
 */
bool SelectDecoys(
    const COutPoint& realOutput,
    size_t ringSize,
    CAmount realAmount,
    const CPubKey& realPubKey,
    const CDecoySelectionParams& params,
    std::vector<CRingMember>& decoys);

/**
 * @brief Select decoys with default parameters
 */
bool SelectDecoys(
    const COutPoint& realOutput,
    size_t ringSize,
    std::vector<CRingMember>& decoys);

/**
 * @brief Build a ring from real output and decoys
 *
 * Creates a CRing with the real output placed at a random position
 * among the decoys.
 *
 * @param realOutput Real output information
 * @param decoys Decoy outputs
 * @param ring [out] The constructed ring
 * @param realIndex [out] Index of real output in the ring
 * @return true if ring construction succeeded
 */
bool BuildRing(
    const CRingMember& realOutput,
    const std::vector<CRingMember>& decoys,
    CRing& ring,
    size_t& realIndex);

} // namespace privacy

#endif // WATTX_PRIVACY_RING_SIGNATURE_H
