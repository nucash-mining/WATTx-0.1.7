// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_PRIVACY_STEALTH_H
#define WATTX_PRIVACY_STEALTH_H

#include <key.h>
#include <pubkey.h>
#include <uint256.h>
#include <serialize.h>

#include <vector>
#include <optional>

namespace privacy {

/**
 * Stealth Address Implementation (DKSAP - Dual-Key Stealth Address Protocol)
 *
 * Allows senders to create one-time addresses that only the intended
 * recipient can spend from, without revealing their identity.
 *
 * Protocol:
 * 1. Recipient publishes stealth address: (scan_pubkey, spend_pubkey)
 * 2. Sender generates ephemeral keypair: (r, R = r*G)
 * 3. Sender computes shared secret: S = r * scan_pubkey
 * 4. Sender derives one-time pubkey: P = spend_pubkey + H(S)*G
 * 5. Sender publishes R in transaction (OP_RETURN or special field)
 * 6. Recipient scans: S' = scan_privkey * R, P' = spend_pubkey + H(S')*G
 * 7. If P' matches output, recipient can spend with: spend_privkey + H(S')
 */

/**
 * @brief Stealth address containing view and spend public keys
 */
class CStealthAddress
{
public:
    // Scan key - used for detecting payments (can be shared with view-only wallets)
    CPubKey scanPubKey;

    // Spend key - used for spending (never shared)
    CPubKey spendPubKey;

    // Optional label for address book
    std::string label;

    // Prefix filter for faster scanning (first N bits of expected pubkey hash)
    uint8_t prefixLength{0};
    uint32_t prefix{0};

    CStealthAddress() = default;
    CStealthAddress(const CPubKey& scan, const CPubKey& spend)
        : scanPubKey(scan), spendPubKey(spend) {}

    bool IsValid() const {
        return scanPubKey.IsValid() && spendPubKey.IsValid();
    }

    /**
     * @brief Encode stealth address to string format
     * Format: "sx1" + base58check(version + scanPubKey + spendPubKey + options)
     */
    std::string ToString() const;

    /**
     * @brief Decode stealth address from string
     */
    static std::optional<CStealthAddress> FromString(const std::string& str);

    SERIALIZE_METHODS(CStealthAddress, obj) {
        READWRITE(obj.scanPubKey, obj.spendPubKey, obj.label, obj.prefixLength, obj.prefix);
    }
};

/**
 * @brief Ephemeral data included in transaction for recipient to recover funds
 */
struct CEphemeralData
{
    // Ephemeral public key R = r*G
    CPubKey ephemeralPubKey;

    // View tag - first byte of shared secret hash for fast filtering
    uint8_t viewTag{0};

    CEphemeralData() = default;
    CEphemeralData(const CPubKey& pubkey, uint8_t tag)
        : ephemeralPubKey(pubkey), viewTag(tag) {}

    SERIALIZE_METHODS(CEphemeralData, obj) {
        READWRITE(obj.ephemeralPubKey, obj.viewTag);
    }
};

/**
 * @brief One-time output derived from stealth address
 */
struct CStealthOutput
{
    // The derived one-time public key P
    CPubKey oneTimePubKey;

    // Ephemeral data for recipient to recover
    CEphemeralData ephemeral;

    // Index in the derivation (for multiple outputs to same stealth address)
    uint32_t outputIndex{0};

    SERIALIZE_METHODS(CStealthOutput, obj) {
        READWRITE(obj.oneTimePubKey, obj.ephemeral, obj.outputIndex);
    }
};

/**
 * @brief Generate a one-time destination for a stealth address
 *
 * @param stealthAddr The recipient's stealth address
 * @param ephemeralPrivKey [out] The generated ephemeral private key (caller stores securely)
 * @param output [out] The stealth output data
 * @return true if generation succeeded
 */
bool GenerateStealthDestination(
    const CStealthAddress& stealthAddr,
    CKey& ephemeralPrivKey,
    CStealthOutput& output);

/**
 * @brief Check if an output belongs to a stealth address (recipient scanning)
 *
 * @param output The stealth output to check
 * @param scanPrivKey The recipient's scan private key
 * @param spendPubKey The recipient's spend public key
 * @param derivedPrivKey [out] If matched, the private key to spend this output
 * @return true if output belongs to this stealth address
 */
bool ScanStealthOutput(
    const CStealthOutput& output,
    const CKey& scanPrivKey,
    const CPubKey& spendPubKey,
    CKey& derivedPrivKey);

/**
 * @brief Derive the private key for spending a stealth output
 *
 * @param scanPrivKey The scan private key
 * @param spendPrivKey The spend private key
 * @param ephemeralPubKey The ephemeral public key from the output
 * @param outputIndex The output index
 * @param derivedKey [out] The derived spending key
 * @return true if derivation succeeded
 */
bool DeriveStealthSpendingKey(
    const CKey& scanPrivKey,
    const CKey& spendPrivKey,
    const CPubKey& ephemeralPubKey,
    uint32_t outputIndex,
    CKey& derivedKey);

/**
 * @brief Compute view tag for fast output filtering
 *
 * @param sharedSecret The ECDH shared secret point
 * @return First byte of H(sharedSecret) as view tag
 */
uint8_t ComputeViewTag(const CPubKey& sharedSecret);

/**
 * @brief Hash shared secret to derive key material
 *
 * @param sharedSecret The ECDH shared secret
 * @param outputIndex Index for unique derivation per output
 * @return 32-byte key derivation material
 */
uint256 HashSharedSecret(const CPubKey& sharedSecret, uint32_t outputIndex);

} // namespace privacy

#endif // WATTX_PRIVACY_STEALTH_H
