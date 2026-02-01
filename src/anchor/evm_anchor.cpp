// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <anchor/evm_anchor.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <logging.h>
#include <streams.h>
#include <util/strencodings.h>

#include <algorithm>
#include <cstring>

namespace evm_anchor {

// ============================================================================
// Global Instance
// ============================================================================

static EVMAnchorManager g_evm_anchor_manager;

EVMAnchorManager& GetEVMAnchorManager() {
    return g_evm_anchor_manager;
}

// ============================================================================
// EVMAnchorData Implementation
// ============================================================================

std::vector<uint8_t> EVMAnchorData::Serialize() const {
    std::vector<uint8_t> result;
    result.reserve(128);  // Pre-allocate

    // Tag and version
    result.push_back(ANCHOR_TAG);
    result.push_back(version);

    // Block height (4 bytes, little-endian)
    result.push_back((wattx_block_height >> 0) & 0xFF);
    result.push_back((wattx_block_height >> 8) & 0xFF);
    result.push_back((wattx_block_height >> 16) & 0xFF);
    result.push_back((wattx_block_height >> 24) & 0xFF);

    // TX count (2 bytes, little-endian)
    result.push_back((evm_tx_count >> 0) & 0xFF);
    result.push_back((evm_tx_count >> 8) & 0xFF);

    // EVM merkle root (32 bytes)
    result.insert(result.end(), evm_merkle_root.begin(), evm_merkle_root.end());

    // State root (32 bytes)
    result.insert(result.end(), state_root.begin(), state_root.end());

    // UTXO root (32 bytes)
    result.insert(result.end(), utxo_root.begin(), utxo_root.end());

    // Timestamp (8 bytes, little-endian)
    for (int i = 0; i < 8; i++) {
        result.push_back((timestamp >> (i * 8)) & 0xFF);
    }

    return result;
}

bool EVMAnchorData::Deserialize(const std::vector<uint8_t>& data) {
    // Minimum size: tag(1) + version(1) + height(4) + count(2) + merkle(32) + state(32) + utxo(32) + time(8) = 112
    if (data.size() < 112) {
        return false;
    }

    size_t pos = 0;

    // Check tag
    if (data[pos++] != ANCHOR_TAG) {
        return false;
    }

    // Version
    version = data[pos++];
    if (version != ANCHOR_VERSION) {
        LogPrintf("EVMAnchor: Unknown version %d\n", version);
        return false;
    }

    // Block height
    wattx_block_height = data[pos] | (data[pos+1] << 8) | (data[pos+2] << 16) | (data[pos+3] << 24);
    pos += 4;

    // TX count
    evm_tx_count = data[pos] | (data[pos+1] << 8);
    pos += 2;

    // EVM merkle root
    std::memcpy(evm_merkle_root.data(), &data[pos], 32);
    pos += 32;

    // State root
    std::memcpy(state_root.data(), &data[pos], 32);
    pos += 32;

    // UTXO root
    std::memcpy(utxo_root.data(), &data[pos], 32);
    pos += 32;

    // Timestamp
    timestamp = 0;
    for (int i = 0; i < 8; i++) {
        timestamp |= (static_cast<int64_t>(data[pos + i]) << (i * 8));
    }

    return true;
}

uint256 EVMAnchorData::GetHash() const {
    auto serialized = Serialize();
    return Hash(serialized);
}

bool EVMAnchorData::IsValid() const {
    // Version check
    if (version != ANCHOR_VERSION) return false;

    // Height must be positive
    if (wattx_block_height == 0) return false;

    // Timestamp must be reasonable (after 2024)
    if (timestamp < 1704067200) return false;  // Jan 1, 2024

    // Merkle root should not be zero if there are transactions
    if (evm_tx_count > 0 && evm_merkle_root.IsNull()) return false;

    return true;
}

// ============================================================================
// ViewKeyAnchor Implementation
// ============================================================================

ViewKeyAnchor ViewKeyAnchor::Create(const EVMAnchorData& data,
                                     const std::array<uint8_t, 32>& view_public_key) {
    ViewKeyAnchor anchor;
    anchor.anchor_data = data;

    // Derive view key tag from public key and anchor hash
    // This allows anyone with the view key to identify and verify anchors
    CSHA256 hasher;
    hasher.Write(view_public_key.data(), 32);
    hasher.Write(data.GetHash().data(), 32);

    uint8_t tag_full[32];
    hasher.Finalize(tag_full);
    std::memcpy(anchor.view_key_tag.data(), tag_full, VIEW_KEY_SIZE);

    // Calculate checksum over all data
    auto serialized = data.Serialize();
    serialized.insert(serialized.end(), anchor.view_key_tag.begin(), anchor.view_key_tag.end());

    CSHA256 checksum_hasher;
    checksum_hasher.Write(serialized.data(), serialized.size());
    uint8_t checksum_full[32];
    checksum_hasher.Finalize(checksum_full);
    std::memcpy(anchor.checksum.data(), checksum_full, CHECKSUM_SIZE);

    return anchor;
}

bool ViewKeyAnchor::Verify(const std::array<uint8_t, 32>& view_public_key) const {
    // Recalculate view key tag
    CSHA256 hasher;
    hasher.Write(view_public_key.data(), 32);
    hasher.Write(anchor_data.GetHash().data(), 32);

    uint8_t expected_tag[32];
    hasher.Finalize(expected_tag);

    // Compare tags
    if (std::memcmp(view_key_tag.data(), expected_tag, VIEW_KEY_SIZE) != 0) {
        return false;
    }

    // Verify checksum
    auto serialized = anchor_data.Serialize();
    serialized.insert(serialized.end(), view_key_tag.begin(), view_key_tag.end());

    CSHA256 checksum_hasher;
    checksum_hasher.Write(serialized.data(), serialized.size());
    uint8_t expected_checksum[32];
    checksum_hasher.Finalize(expected_checksum);

    return std::memcmp(checksum.data(), expected_checksum, CHECKSUM_SIZE) == 0;
}

std::vector<uint8_t> ViewKeyAnchor::Serialize() const {
    std::vector<uint8_t> result;

    // Anchor data
    auto anchor_bytes = anchor_data.Serialize();
    result.insert(result.end(), anchor_bytes.begin(), anchor_bytes.end());

    // View key tag
    result.insert(result.end(), view_key_tag.begin(), view_key_tag.end());

    // Checksum
    result.insert(result.end(), checksum.begin(), checksum.end());

    return result;
}

bool ViewKeyAnchor::Deserialize(const std::vector<uint8_t>& data,
                                 const std::array<uint8_t, 32>& view_public_key,
                                 ViewKeyAnchor& out) {
    // Minimum size: anchor data (112) + view key tag (32) + checksum (4) = 148
    if (data.size() < 148) {
        return false;
    }

    // Deserialize anchor data
    if (!out.anchor_data.Deserialize(data)) {
        return false;
    }

    // Extract view key tag
    size_t tag_offset = 112;  // After anchor data
    std::memcpy(out.view_key_tag.data(), &data[tag_offset], VIEW_KEY_SIZE);

    // Extract checksum
    size_t checksum_offset = tag_offset + VIEW_KEY_SIZE;
    std::memcpy(out.checksum.data(), &data[checksum_offset], CHECKSUM_SIZE);

    // Verify with view key
    return out.Verify(view_public_key);
}

// ============================================================================
// EVMAnchorManager Implementation
// ============================================================================

EVMAnchorManager::EVMAnchorManager() = default;

EVMAnchorManager::~EVMAnchorManager() = default;

bool EVMAnchorManager::Initialize(const std::array<uint8_t, 32>& view_public_key) {
    LOCK(m_mutex);

    if (m_initialized) {
        LogPrintf("EVMAnchor: Already initialized\n");
        return true;
    }

    m_view_public_key = view_public_key;
    m_initialized = true;

    LogPrintf("EVMAnchor: Initialized with view key %s...\n",
              HexStr(Span{view_public_key.data(), 8}));
    LogPrintf("EVMAnchor: Activation height: %d\n", m_activation_height);

    return true;
}

EVMAnchorData EVMAnchorManager::CreateAnchor(const CBlock& block, int height) {
    // Collect EVM transaction hashes
    std::vector<uint256> evm_tx_hashes = GetEVMTransactionHashes(block);

    return CreateAnchor(height, evm_tx_hashes,
                        block.hashStateRoot, block.hashUTXORoot,
                        block.nTime);
}

EVMAnchorData EVMAnchorManager::CreateAnchor(int height,
                                              const std::vector<uint256>& evm_tx_hashes,
                                              const uint256& state_root,
                                              const uint256& utxo_root,
                                              int64_t timestamp) {
    EVMAnchorData anchor;
    anchor.version = ANCHOR_VERSION;
    anchor.wattx_block_height = height;
    anchor.evm_tx_count = std::min(static_cast<size_t>(UINT16_MAX), evm_tx_hashes.size());
    anchor.evm_merkle_root = ComputeEVMMerkleRoot(evm_tx_hashes);
    anchor.state_root = state_root;
    anchor.utxo_root = utxo_root;
    anchor.timestamp = timestamp;

    // Update statistics
    m_total_anchors++;
    m_total_evm_tx += anchor.evm_tx_count;

    LogPrintf("EVMAnchor: Created anchor for block %d with %d EVM txs, merkle root: %s\n",
              height, anchor.evm_tx_count,
              anchor.evm_merkle_root.GetHex().substr(0, 16));

    return anchor;
}

ViewKeyAnchor EVMAnchorManager::CreateViewKeyAnchor(const EVMAnchorData& data) {
    LOCK(m_mutex);

    if (!m_initialized) {
        LogPrintf("EVMAnchor: Warning - creating anchor without initialization\n");
    }

    return ViewKeyAnchor::Create(data, m_view_public_key);
}

std::vector<uint8_t> EVMAnchorManager::BuildAnchorTag(const EVMAnchorData& data) {
    LOCK(m_mutex);

    // Create view key anchor
    ViewKeyAnchor vk_anchor = ViewKeyAnchor::Create(data, m_view_public_key);

    // Serialize with WATTx anchor tag prefix
    std::vector<uint8_t> tag;

    // TX_EXTRA_NONCE tag (0x02) followed by length
    // This is standard Monero extra field format
    auto anchor_bytes = vk_anchor.Serialize();

    tag.push_back(0x02);  // TX_EXTRA_NONCE
    tag.push_back(static_cast<uint8_t>(anchor_bytes.size() + 1));  // Length + 1 for subtag
    tag.push_back(ANCHOR_TAG);  // WATTx anchor subtag
    tag.insert(tag.end(), anchor_bytes.begin(), anchor_bytes.end());

    return tag;
}

bool EVMAnchorManager::ParseAnchorTag(const std::vector<uint8_t>& extra, EVMAnchorData& out) {
    LOCK(m_mutex);

    // Search for WATTx anchor tag in extra field
    for (size_t i = 0; i + 3 < extra.size(); i++) {
        // Look for TX_EXTRA_NONCE (0x02) followed by length and WATTx tag
        if (extra[i] == 0x02) {
            size_t len = extra[i + 1];
            if (i + 2 + len <= extra.size() && extra[i + 2] == ANCHOR_TAG) {
                // Found WATTx anchor tag
                std::vector<uint8_t> anchor_data(extra.begin() + i + 3,
                                                  extra.begin() + i + 2 + len);

                ViewKeyAnchor vk_anchor;
                if (ViewKeyAnchor::Deserialize(anchor_data, m_view_public_key, vk_anchor)) {
                    out = vk_anchor.anchor_data;
                    return true;
                }
            }
        }
    }

    return false;
}

std::vector<uint256> EVMAnchorManager::GetEVMTransactionHashes(const CBlock& block) {
    std::vector<uint256> evm_hashes;

    for (const auto& tx : block.vtx) {
        // Check if transaction involves EVM (create or call)
        // This checks for OP_CREATE or OP_CALL in outputs
        bool is_evm_tx = false;

        for (const auto& out : tx->vout) {
            const auto& script = out.scriptPubKey;
            // Check for EVM opcodes (OP_CREATE = 0xc1, OP_CALL = 0xc2)
            if (script.size() >= 1) {
                for (size_t i = 0; i < script.size(); i++) {
                    if (script[i] == 0xc1 || script[i] == 0xc2) {
                        is_evm_tx = true;
                        break;
                    }
                }
            }
            if (is_evm_tx) break;
        }

        if (is_evm_tx) {
            evm_hashes.push_back(tx->GetHash());
        }
    }

    return evm_hashes;
}

uint256 EVMAnchorManager::ComputeEVMMerkleRoot(const std::vector<uint256>& hashes) {
    if (hashes.empty()) {
        return uint256{};
    }

    if (hashes.size() == 1) {
        return hashes[0];
    }

    // Standard Bitcoin-style merkle tree computation
    std::vector<uint256> nodes = hashes;

    while (nodes.size() > 1) {
        std::vector<uint256> new_nodes;

        for (size_t i = 0; i < nodes.size(); i += 2) {
            uint256 combined;
            if (i + 1 < nodes.size()) {
                combined = Hash(nodes[i], nodes[i + 1]);
            } else {
                // Odd number: duplicate last hash
                combined = Hash(nodes[i], nodes[i]);
            }
            new_nodes.push_back(combined);
        }

        nodes = std::move(new_nodes);
    }

    return nodes[0];
}

EVMAnchorData EVMAnchorManager::GetPendingAnchor() const {
    LOCK(m_mutex);
    return m_pending_anchor;
}

void EVMAnchorManager::SetPendingAnchor(const EVMAnchorData& anchor) {
    LOCK(m_mutex);
    m_pending_anchor = anchor;
    m_has_pending_anchor = true;

    LogPrintf("EVMAnchor: Set pending anchor for block %d\n",
              anchor.wattx_block_height);
}

void EVMAnchorManager::ClearPendingAnchor() {
    LOCK(m_mutex);
    m_pending_anchor = EVMAnchorData{};
    m_has_pending_anchor = false;
}

std::array<uint8_t, VIEW_KEY_SIZE> EVMAnchorManager::DeriveViewKeyTag(
    const std::array<uint8_t, 32>& view_public_key,
    const uint256& anchor_hash) const {

    std::array<uint8_t, VIEW_KEY_SIZE> tag;

    CSHA256 hasher;
    hasher.Write(view_public_key.data(), 32);
    hasher.Write(anchor_hash.data(), 32);
    hasher.Write((const uint8_t*)"WATTx_ANCHOR_TAG", 16);

    uint8_t full_hash[32];
    hasher.Finalize(full_hash);
    std::memcpy(tag.data(), full_hash, VIEW_KEY_SIZE);

    return tag;
}

std::array<uint8_t, CHECKSUM_SIZE> EVMAnchorManager::CalculateChecksum(
    const std::vector<uint8_t>& data) const {

    std::array<uint8_t, CHECKSUM_SIZE> checksum;

    CSHA256 hasher;
    hasher.Write(data.data(), data.size());

    uint8_t full_hash[32];
    hasher.Finalize(full_hash);
    std::memcpy(checksum.data(), full_hash, CHECKSUM_SIZE);

    return checksum;
}

}  // namespace evm_anchor
