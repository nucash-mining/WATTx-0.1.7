// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_ANCHOR_PRIVATE_SWAP_H
#define WATTX_ANCHOR_PRIVATE_SWAP_H

#include <anchor/evm_anchor.h>
#include <uint256.h>

#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace private_swap {

// Supported chain types for cross-chain swaps
enum class ChainType {
    WATTX_EVM,    // WATTx EVM transactions
    WATTX_UTXO,   // WATTx native UTXO
    MONERO,       // Monero (XMR)
    SOLANA,       // Solana (SOL)
    XRP_LEDGER,   // XRP Ledger
    XPL,          // XPL chain
    ETHEREUM,     // Ethereum mainnet
    BSC,          // Binance Smart Chain
    POLYGON,      // Polygon
    CUSTOM        // Custom chain with identifier
};

// Magic bytes for private swap identification
static constexpr uint8_t PRIVATE_SWAP_TAG = 0x50;     // 'P' for Private
static constexpr uint8_t PRIVATE_SWAP_VERSION = 0x01;

/**
 * SwapParticipant - Represents one party in a swap
 */
struct SwapParticipant {
    ChainType chain_type;
    std::string chain_identifier;         // Chain-specific ID (e.g., "mainnet", "testnet")
    std::string address;                  // Address on the chain
    std::array<uint8_t, 32> view_key;     // Participant's view key for this swap

    bool IsValid() const;
};

/**
 * PrivateSwapData - Encrypted swap data anchored to Monero
 * Only readable by participants who have the swap's view key
 */
struct PrivateSwapData {
    uint8_t version{PRIVATE_SWAP_VERSION};

    // Swap identification
    uint256 swap_id;                      // Unique swap identifier
    uint64_t created_at;                  // Creation timestamp
    uint64_t expires_at;                  // Expiration timestamp

    // Source chain data
    ChainType source_chain;
    std::string source_address;
    uint64_t source_amount;
    std::string source_asset;             // Token contract or native

    // Destination chain data
    ChainType dest_chain;
    std::string dest_address;
    uint64_t dest_amount;
    std::string dest_asset;

    // Atomic swap parameters
    uint256 hash_lock;                    // HTLC hash lock
    uint64_t time_lock;                   // HTLC time lock

    // State tracking
    enum class SwapState {
        INITIATED,
        PARTICIPANT_JOINED,
        SOURCE_FUNDED,
        DEST_FUNDED,
        CLAIMED,
        REFUNDED,
        EXPIRED
    };
    SwapState state{SwapState::INITIATED};

    // EVM-specific data (for WATTX_EVM, ETHEREUM, BSC, POLYGON)
    uint256 evm_tx_hash;                  // Transaction hash on EVM chain
    uint256 evm_state_root;               // State root after tx
    std::vector<uint8_t> evm_receipt;     // Transaction receipt (compact)

    // Serialize for embedding (encrypted with view key)
    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& data);
    uint256 GetHash() const;
    bool IsValid() const;
};

/**
 * EncryptedSwapAnchor - Swap data encrypted with participant view keys
 * Embedded in Monero blocks via coinbase extra field
 */
struct EncryptedSwapAnchor {
    std::array<uint8_t, 32> swap_key_tag; // Derived from swap view key
    std::vector<uint8_t> encrypted_data;  // XOR-encrypted swap data
    std::array<uint8_t, 4> checksum;      // Integrity check

    // Create from swap data and view key
    static EncryptedSwapAnchor Create(const PrivateSwapData& data,
                                       const std::array<uint8_t, 32>& view_key);

    // Decrypt and verify with view key
    static bool Decrypt(const EncryptedSwapAnchor& anchor,
                        const std::array<uint8_t, 32>& view_key,
                        PrivateSwapData& out);

    // Serialize for Monero extra field
    std::vector<uint8_t> Serialize() const;
    static bool Deserialize(const std::vector<uint8_t>& data, EncryptedSwapAnchor& out);
};

/**
 * PrivateSwapManager - Manages private cross-chain swaps
 */
class PrivateSwapManager {
public:
    PrivateSwapManager();
    ~PrivateSwapManager();

    /**
     * Generate a new view key for a swap
     * This key is shared between swap participants
     */
    std::array<uint8_t, 32> GenerateSwapViewKey();

    /**
     * Derive a deterministic view key from swap parameters
     * Both parties can independently derive the same key
     */
    std::array<uint8_t, 32> DeriveSwapViewKey(
        const std::string& initiator_address,
        const std::string& participant_address,
        uint64_t amount,
        uint64_t timestamp);

    /**
     * Initiate a private swap
     * Returns the swap ID and view key
     */
    std::pair<uint256, std::array<uint8_t, 32>> InitiateSwap(
        ChainType source_chain,
        const std::string& source_address,
        uint64_t source_amount,
        const std::string& source_asset,
        ChainType dest_chain,
        const std::string& dest_address,
        uint64_t dest_amount,
        const std::string& dest_asset,
        uint64_t time_lock_seconds);

    /**
     * Join an existing swap as participant
     */
    bool JoinSwap(const uint256& swap_id, const std::array<uint8_t, 32>& view_key);

    /**
     * Get swap data by ID (requires view key)
     */
    bool GetSwap(const uint256& swap_id,
                 const std::array<uint8_t, 32>& view_key,
                 PrivateSwapData& out);

    /**
     * Update swap state
     */
    bool UpdateSwapState(const uint256& swap_id,
                         const std::array<uint8_t, 32>& view_key,
                         PrivateSwapData::SwapState new_state);

    /**
     * Record EVM transaction for swap
     */
    bool RecordEVMTransaction(const uint256& swap_id,
                               const std::array<uint8_t, 32>& view_key,
                               const uint256& tx_hash,
                               const uint256& state_root,
                               const std::vector<uint8_t>& receipt);

    /**
     * Build anchor tag for Monero coinbase extra
     */
    std::vector<uint8_t> BuildSwapAnchorTag(const PrivateSwapData& data,
                                             const std::array<uint8_t, 32>& view_key);

    /**
     * Parse swap from Monero coinbase extra (requires view key)
     */
    bool ParseSwapAnchor(const std::vector<uint8_t>& extra,
                          const std::array<uint8_t, 32>& view_key,
                          PrivateSwapData& out);

    /**
     * Get all swaps accessible with a view key
     */
    std::vector<PrivateSwapData> GetSwapsForViewKey(
        const std::array<uint8_t, 32>& view_key);

    // Statistics
    uint64_t GetTotalSwaps() const { return m_total_swaps; }
    uint64_t GetActiveSwaps() const { return m_active_swaps; }

private:
    mutable std::mutex m_mutex;

    // Swap storage (encrypted)
    std::map<uint256, EncryptedSwapAnchor> m_swaps;

    // Statistics
    uint64_t m_total_swaps{0};
    uint64_t m_active_swaps{0};

    // Generate hash lock for HTLC
    uint256 GenerateHashLock(const uint256& preimage);

    // Encrypt data with view key
    std::vector<uint8_t> EncryptWithViewKey(const std::vector<uint8_t>& data,
                                             const std::array<uint8_t, 32>& view_key);

    // Decrypt data with view key
    std::vector<uint8_t> DecryptWithViewKey(const std::vector<uint8_t>& encrypted,
                                             const std::array<uint8_t, 32>& view_key);
};

// Global instance
PrivateSwapManager& GetPrivateSwapManager();

// Helper functions for chain type
std::string ChainTypeToString(ChainType type);
ChainType StringToChainType(const std::string& str);

}  // namespace private_swap

#endif // WATTX_ANCHOR_PRIVATE_SWAP_H
