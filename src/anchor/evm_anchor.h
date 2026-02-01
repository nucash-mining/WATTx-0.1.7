// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_ANCHOR_EVM_ANCHOR_H
#define WATTX_ANCHOR_EVM_ANCHOR_H

#include <uint256.h>
#include <primitives/block.h>
#include <sync.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace evm_anchor {

// Magic bytes for anchor identification in Monero extra field
static constexpr uint8_t ANCHOR_TAG = 0x57;  // 'W' for WATTx
static constexpr uint8_t ANCHOR_VERSION = 0x01;

// Anchor data structure sizes
static constexpr size_t VIEW_KEY_SIZE = 32;
static constexpr size_t ANCHOR_HASH_SIZE = 32;
static constexpr size_t BLOCK_HEIGHT_SIZE = 4;
static constexpr size_t TX_COUNT_SIZE = 2;
static constexpr size_t CHECKSUM_SIZE = 4;

/**
 * EVMAnchorData - Reference data for WATTx EVM transactions
 * This is the compact representation that gets anchored to Monero blocks
 */
struct EVMAnchorData {
    uint8_t version{ANCHOR_VERSION};
    uint32_t wattx_block_height{0};       // WATTx block being anchored
    uint16_t evm_tx_count{0};             // Number of EVM transactions
    uint256 evm_merkle_root;              // Merkle root of EVM tx hashes
    uint256 state_root;                   // Post-block EVM state root
    uint256 utxo_root;                    // Post-block UTXO root
    int64_t timestamp{0};                 // Block timestamp

    // Serialize for embedding in Monero coinbase
    std::vector<uint8_t> Serialize() const;

    // Deserialize from Monero coinbase extra
    bool Deserialize(const std::vector<uint8_t>& data);

    // Get unique anchor identifier
    uint256 GetHash() const;

    // Check if anchor data is valid
    bool IsValid() const;
};

/**
 * ViewKeyAnchor - Anchor encrypted/derived with view key
 * Allows anyone with the public view key to read anchor data
 */
struct ViewKeyAnchor {
    std::array<uint8_t, VIEW_KEY_SIZE> view_key_tag;  // Derived from public view key
    EVMAnchorData anchor_data;
    std::array<uint8_t, CHECKSUM_SIZE> checksum;

    // Create from anchor data and view key
    static ViewKeyAnchor Create(const EVMAnchorData& data,
                                 const std::array<uint8_t, 32>& view_public_key);

    // Verify anchor can be read with given view key
    bool Verify(const std::array<uint8_t, 32>& view_public_key) const;

    // Serialize for Monero coinbase extra
    std::vector<uint8_t> Serialize() const;

    // Deserialize and verify with view key
    static bool Deserialize(const std::vector<uint8_t>& data,
                            const std::array<uint8_t, 32>& view_public_key,
                            ViewKeyAnchor& out);
};

/**
 * EVMAnchorManager - Manages creation and verification of EVM anchors
 */
class EVMAnchorManager {
public:
    EVMAnchorManager();
    ~EVMAnchorManager();

    // Initialize with WATTx view key
    bool Initialize(const std::array<uint8_t, 32>& view_public_key);

    // Set activation height
    void SetActivationHeight(int height) { m_activation_height = height; }
    int GetActivationHeight() const { return m_activation_height; }

    // Check if anchoring is active at given height
    bool IsActive(int height) const { return height >= m_activation_height; }

    /**
     * Create anchor from WATTx block
     * Collects EVM tx hashes and creates anchor commitment
     */
    EVMAnchorData CreateAnchor(const CBlock& block, int height);

    /**
     * Create anchor from explicit data
     */
    EVMAnchorData CreateAnchor(int height,
                                const std::vector<uint256>& evm_tx_hashes,
                                const uint256& state_root,
                                const uint256& utxo_root,
                                int64_t timestamp);

    /**
     * Create view key anchor for embedding in Monero
     */
    ViewKeyAnchor CreateViewKeyAnchor(const EVMAnchorData& data);

    /**
     * Build the anchor tag for Monero coinbase extra field
     * This is what gets embedded in Monero blocks
     */
    std::vector<uint8_t> BuildAnchorTag(const EVMAnchorData& data);

    /**
     * Parse anchor from Monero coinbase extra field
     */
    bool ParseAnchorTag(const std::vector<uint8_t>& extra, EVMAnchorData& out);

    /**
     * Get EVM transaction hashes from a block
     */
    std::vector<uint256> GetEVMTransactionHashes(const CBlock& block);

    /**
     * Compute merkle root of EVM transaction hashes
     */
    uint256 ComputeEVMMerkleRoot(const std::vector<uint256>& hashes);

    // Get the current pending anchor (for merged mining)
    EVMAnchorData GetPendingAnchor() const;

    // Set pending anchor from new block
    void SetPendingAnchor(const EVMAnchorData& anchor);

    // Clear pending anchor
    void ClearPendingAnchor();

    // Statistics
    uint64_t GetTotalAnchors() const { return m_total_anchors; }
    uint64_t GetTotalEVMTxAnchored() const { return m_total_evm_tx; }

    // Get public view key (for verification)
    std::array<uint8_t, 32> GetViewPublicKey() const { return m_view_public_key; }

private:
    mutable Mutex m_mutex;

    bool m_initialized{false};
    int m_activation_height{50000};  // Default, can be overridden

    std::array<uint8_t, 32> m_view_public_key;

    EVMAnchorData m_pending_anchor;
    bool m_has_pending_anchor{false};

    // Statistics
    std::atomic<uint64_t> m_total_anchors{0};
    std::atomic<uint64_t> m_total_evm_tx{0};

    // Derive tag from view key for anchor identification
    std::array<uint8_t, VIEW_KEY_SIZE> DeriveViewKeyTag(
        const std::array<uint8_t, 32>& view_public_key,
        const uint256& anchor_hash) const;

    // Calculate checksum
    std::array<uint8_t, CHECKSUM_SIZE> CalculateChecksum(
        const std::vector<uint8_t>& data) const;
};

// Global anchor manager instance
EVMAnchorManager& GetEVMAnchorManager();

}  // namespace evm_anchor

#endif // WATTX_ANCHOR_EVM_ANCHOR_H
