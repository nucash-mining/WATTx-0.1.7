// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <anchor/private_swap.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <logging.h>
#include <random.h>
#include <streams.h>
#include <util/strencodings.h>
#include <util/time.h>

#include <algorithm>
#include <cstring>

namespace private_swap {

// ============================================================================
// Global Instance
// ============================================================================

static PrivateSwapManager g_private_swap_manager;

PrivateSwapManager& GetPrivateSwapManager() {
    return g_private_swap_manager;
}

// ============================================================================
// Helper Functions
// ============================================================================

std::string ChainTypeToString(ChainType type) {
    switch (type) {
        case ChainType::WATTX_EVM: return "WATTX_EVM";
        case ChainType::WATTX_UTXO: return "WATTX_UTXO";
        case ChainType::MONERO: return "MONERO";
        case ChainType::SOLANA: return "SOLANA";
        case ChainType::XRP_LEDGER: return "XRP_LEDGER";
        case ChainType::XPL: return "XPL";
        case ChainType::ETHEREUM: return "ETHEREUM";
        case ChainType::BSC: return "BSC";
        case ChainType::POLYGON: return "POLYGON";
        case ChainType::CUSTOM: return "CUSTOM";
    }
    return "UNKNOWN";
}

ChainType StringToChainType(const std::string& str) {
    if (str == "WATTX_EVM") return ChainType::WATTX_EVM;
    if (str == "WATTX_UTXO") return ChainType::WATTX_UTXO;
    if (str == "MONERO" || str == "XMR") return ChainType::MONERO;
    if (str == "SOLANA" || str == "SOL") return ChainType::SOLANA;
    if (str == "XRP_LEDGER" || str == "XRP") return ChainType::XRP_LEDGER;
    if (str == "XPL") return ChainType::XPL;
    if (str == "ETHEREUM" || str == "ETH") return ChainType::ETHEREUM;
    if (str == "BSC" || str == "BNB") return ChainType::BSC;
    if (str == "POLYGON" || str == "MATIC") return ChainType::POLYGON;
    return ChainType::CUSTOM;
}

// ============================================================================
// SwapParticipant Implementation
// ============================================================================

bool SwapParticipant::IsValid() const {
    return !address.empty();
}

// ============================================================================
// PrivateSwapData Implementation
// ============================================================================

std::vector<uint8_t> PrivateSwapData::Serialize() const {
    DataStream ss{};

    // Header
    ss << PRIVATE_SWAP_TAG;
    ss << version;

    // Swap ID
    ss << swap_id;

    // Timestamps
    ss << created_at;
    ss << expires_at;

    // Source chain
    ss << static_cast<uint8_t>(source_chain);
    ss << source_address;
    ss << source_amount;
    ss << source_asset;

    // Dest chain
    ss << static_cast<uint8_t>(dest_chain);
    ss << dest_address;
    ss << dest_amount;
    ss << dest_asset;

    // HTLC params
    ss << hash_lock;
    ss << time_lock;

    // State
    ss << static_cast<uint8_t>(state);

    // EVM data
    ss << evm_tx_hash;
    ss << evm_state_root;
    ss << evm_receipt;

    std::vector<uint8_t> result;
    result.reserve(ss.size());
    for (auto byte : ss) {
        result.push_back(static_cast<uint8_t>(byte));
    }
    return result;
}

bool PrivateSwapData::Deserialize(const std::vector<uint8_t>& data) {
    try {
        DataStream ss(data);

        // Header
        uint8_t tag, ver;
        ss >> tag >> ver;
        if (tag != PRIVATE_SWAP_TAG || ver != PRIVATE_SWAP_VERSION) {
            return false;
        }
        version = ver;

        // Swap ID
        ss >> swap_id;

        // Timestamps
        ss >> created_at;
        ss >> expires_at;

        // Source chain
        uint8_t src_chain;
        ss >> src_chain;
        source_chain = static_cast<ChainType>(src_chain);
        ss >> source_address;
        ss >> source_amount;
        ss >> source_asset;

        // Dest chain
        uint8_t dst_chain;
        ss >> dst_chain;
        dest_chain = static_cast<ChainType>(dst_chain);
        ss >> dest_address;
        ss >> dest_amount;
        ss >> dest_asset;

        // HTLC params
        ss >> hash_lock;
        ss >> time_lock;

        // State
        uint8_t st;
        ss >> st;
        state = static_cast<SwapState>(st);

        // EVM data
        ss >> evm_tx_hash;
        ss >> evm_state_root;
        ss >> evm_receipt;

        return true;
    } catch (...) {
        return false;
    }
}

uint256 PrivateSwapData::GetHash() const {
    auto serialized = Serialize();
    return Hash(serialized);
}

bool PrivateSwapData::IsValid() const {
    if (version != PRIVATE_SWAP_VERSION) return false;
    if (swap_id.IsNull()) return false;
    if (created_at == 0) return false;
    if (source_address.empty() || dest_address.empty()) return false;
    if (source_amount == 0 && dest_amount == 0) return false;
    return true;
}

// ============================================================================
// EncryptedSwapAnchor Implementation
// ============================================================================

EncryptedSwapAnchor EncryptedSwapAnchor::Create(const PrivateSwapData& data,
                                                 const std::array<uint8_t, 32>& view_key) {
    EncryptedSwapAnchor anchor;

    // Derive swap key tag from view key and swap ID
    CSHA256 hasher;
    hasher.Write(view_key.data(), 32);
    hasher.Write(data.swap_id.data(), 32);
    hasher.Write((const uint8_t*)"PRIVATE_SWAP_TAG", 16);

    uint8_t tag_full[32];
    hasher.Finalize(tag_full);
    std::memcpy(anchor.swap_key_tag.data(), tag_full, 32);

    // Serialize and encrypt data
    auto plaintext = data.Serialize();

    // XOR encryption with key stream derived from view key
    anchor.encrypted_data.resize(plaintext.size());
    CSHA256 key_stream;
    key_stream.Write(view_key.data(), 32);
    key_stream.Write(data.swap_id.data(), 32);

    uint8_t key_block[32];
    size_t key_pos = 32;

    for (size_t i = 0; i < plaintext.size(); i++) {
        if (key_pos >= 32) {
            key_stream.Finalize(key_block);
            key_stream.Reset();
            key_stream.Write(key_block, 32);
            key_stream.Write((const uint8_t*)"NEXT", 4);
            key_pos = 0;
        }
        anchor.encrypted_data[i] = plaintext[i] ^ key_block[key_pos++];
    }

    // Calculate checksum
    CSHA256 checksum_hasher;
    checksum_hasher.Write(anchor.swap_key_tag.data(), 32);
    checksum_hasher.Write(anchor.encrypted_data.data(), anchor.encrypted_data.size());

    uint8_t checksum_full[32];
    checksum_hasher.Finalize(checksum_full);
    std::memcpy(anchor.checksum.data(), checksum_full, 4);

    return anchor;
}

bool EncryptedSwapAnchor::Decrypt(const EncryptedSwapAnchor& anchor,
                                   const std::array<uint8_t, 32>& view_key,
                                   PrivateSwapData& out) {
    // First, try to deserialize to get swap_id for verification
    // We'll do a trial decryption with a dummy swap_id first

    // For verification, we need to try decrypting
    // Since we don't know the swap_id yet, we'll verify the checksum first
    CSHA256 checksum_hasher;
    checksum_hasher.Write(anchor.swap_key_tag.data(), 32);
    checksum_hasher.Write(anchor.encrypted_data.data(), anchor.encrypted_data.size());

    uint8_t expected_checksum[32];
    checksum_hasher.Finalize(expected_checksum);

    if (std::memcmp(anchor.checksum.data(), expected_checksum, 4) != 0) {
        return false;
    }

    // Try decryption with multiple potential swap IDs
    // In practice, the caller would provide candidate swap IDs or
    // we'd iterate through known swaps

    // For now, we'll attempt decryption and validate the result
    std::vector<uint8_t> decrypted(anchor.encrypted_data.size());

    // We need to extract the swap_id from the encrypted data first
    // The swap_id is at bytes 2-34 (after tag and version)
    // For initial decryption, use view_key directly

    CSHA256 key_stream;
    key_stream.Write(view_key.data(), 32);
    // Use first part of encrypted data as IV
    uint8_t iv[32];
    std::memset(iv, 0, 32);
    key_stream.Write(iv, 32);

    uint8_t key_block[32];
    size_t key_pos = 32;

    for (size_t i = 0; i < anchor.encrypted_data.size(); i++) {
        if (key_pos >= 32) {
            key_stream.Finalize(key_block);
            key_stream.Reset();
            key_stream.Write(key_block, 32);
            key_stream.Write((const uint8_t*)"NEXT", 4);
            key_pos = 0;
        }
        decrypted[i] = anchor.encrypted_data[i] ^ key_block[key_pos++];
    }

    // Try to deserialize
    if (!out.Deserialize(decrypted)) {
        return false;
    }

    // Verify by re-encrypting and comparing
    auto re_anchor = Create(out, view_key);
    return re_anchor.swap_key_tag == anchor.swap_key_tag;
}

std::vector<uint8_t> EncryptedSwapAnchor::Serialize() const {
    std::vector<uint8_t> result;

    // Tag
    result.push_back(PRIVATE_SWAP_TAG);

    // Length of encrypted data (2 bytes, little-endian)
    result.push_back((encrypted_data.size() >> 0) & 0xFF);
    result.push_back((encrypted_data.size() >> 8) & 0xFF);

    // Swap key tag
    result.insert(result.end(), swap_key_tag.begin(), swap_key_tag.end());

    // Encrypted data
    result.insert(result.end(), encrypted_data.begin(), encrypted_data.end());

    // Checksum
    result.insert(result.end(), checksum.begin(), checksum.end());

    return result;
}

bool EncryptedSwapAnchor::Deserialize(const std::vector<uint8_t>& data, EncryptedSwapAnchor& out) {
    if (data.size() < 40) return false;  // Minimum: tag + len + swap_key_tag + checksum

    size_t pos = 0;

    // Check tag
    if (data[pos++] != PRIVATE_SWAP_TAG) return false;

    // Get length
    size_t enc_len = data[pos] | (data[pos + 1] << 8);
    pos += 2;

    if (data.size() < pos + 32 + enc_len + 4) return false;

    // Swap key tag
    std::memcpy(out.swap_key_tag.data(), &data[pos], 32);
    pos += 32;

    // Encrypted data
    out.encrypted_data.assign(data.begin() + pos, data.begin() + pos + enc_len);
    pos += enc_len;

    // Checksum
    std::memcpy(out.checksum.data(), &data[pos], 4);

    return true;
}

// ============================================================================
// PrivateSwapManager Implementation
// ============================================================================

PrivateSwapManager::PrivateSwapManager() = default;
PrivateSwapManager::~PrivateSwapManager() = default;

std::array<uint8_t, 32> PrivateSwapManager::GenerateSwapViewKey() {
    std::array<uint8_t, 32> key;
    GetRandBytes(Span{key.data(), 32});
    return key;
}

std::array<uint8_t, 32> PrivateSwapManager::DeriveSwapViewKey(
    const std::string& initiator_address,
    const std::string& participant_address,
    uint64_t amount,
    uint64_t timestamp) {

    std::array<uint8_t, 32> key;

    CSHA256 hasher;
    hasher.Write((const uint8_t*)initiator_address.data(), initiator_address.size());
    hasher.Write((const uint8_t*)participant_address.data(), participant_address.size());
    hasher.Write((const uint8_t*)&amount, sizeof(amount));
    hasher.Write((const uint8_t*)&timestamp, sizeof(timestamp));
    hasher.Write((const uint8_t*)"WATTX_SWAP_VIEW_KEY", 19);

    hasher.Finalize(key.data());
    return key;
}

std::pair<uint256, std::array<uint8_t, 32>> PrivateSwapManager::InitiateSwap(
    ChainType source_chain,
    const std::string& source_address,
    uint64_t source_amount,
    const std::string& source_asset,
    ChainType dest_chain,
    const std::string& dest_address,
    uint64_t dest_amount,
    const std::string& dest_asset,
    uint64_t time_lock_seconds) {

    std::lock_guard<std::mutex> lock(m_mutex);

    PrivateSwapData swap;
    swap.version = PRIVATE_SWAP_VERSION;
    swap.created_at = GetTime();
    swap.expires_at = swap.created_at + time_lock_seconds;

    swap.source_chain = source_chain;
    swap.source_address = source_address;
    swap.source_amount = source_amount;
    swap.source_asset = source_asset;

    swap.dest_chain = dest_chain;
    swap.dest_address = dest_address;
    swap.dest_amount = dest_amount;
    swap.dest_asset = dest_asset;

    swap.time_lock = time_lock_seconds;
    swap.state = PrivateSwapData::SwapState::INITIATED;

    // Generate preimage and hash lock
    uint256 preimage;
    GetRandBytes(Span{preimage.data(), 32});
    swap.hash_lock = GenerateHashLock(preimage);

    // Generate swap ID
    CSHA256 hasher;
    hasher.Write((const uint8_t*)&swap.created_at, sizeof(swap.created_at));
    hasher.Write((const uint8_t*)source_address.data(), source_address.size());
    hasher.Write((const uint8_t*)dest_address.data(), dest_address.size());
    hasher.Write(preimage.data(), 32);
    hasher.Finalize(swap.swap_id.data());

    // Generate view key for this swap
    auto view_key = DeriveSwapViewKey(source_address, dest_address, source_amount, swap.created_at);

    // Store encrypted swap
    auto encrypted = EncryptedSwapAnchor::Create(swap, view_key);
    m_swaps[swap.swap_id] = encrypted;

    m_total_swaps++;
    m_active_swaps++;

    LogPrintf("PrivateSwap: Initiated swap %s (%s -> %s)\n",
              swap.swap_id.GetHex().substr(0, 16),
              ChainTypeToString(source_chain),
              ChainTypeToString(dest_chain));

    return {swap.swap_id, view_key};
}

bool PrivateSwapManager::JoinSwap(const uint256& swap_id, const std::array<uint8_t, 32>& view_key) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_swaps.find(swap_id);
    if (it == m_swaps.end()) {
        return false;
    }

    PrivateSwapData swap;
    if (!EncryptedSwapAnchor::Decrypt(it->second, view_key, swap)) {
        return false;
    }

    if (swap.state != PrivateSwapData::SwapState::INITIATED) {
        return false;
    }

    swap.state = PrivateSwapData::SwapState::PARTICIPANT_JOINED;

    // Re-encrypt and store
    auto encrypted = EncryptedSwapAnchor::Create(swap, view_key);
    m_swaps[swap_id] = encrypted;

    LogPrintf("PrivateSwap: Participant joined swap %s\n", swap_id.GetHex().substr(0, 16));

    return true;
}

bool PrivateSwapManager::GetSwap(const uint256& swap_id,
                                  const std::array<uint8_t, 32>& view_key,
                                  PrivateSwapData& out) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_swaps.find(swap_id);
    if (it == m_swaps.end()) {
        return false;
    }

    return EncryptedSwapAnchor::Decrypt(it->second, view_key, out);
}

bool PrivateSwapManager::UpdateSwapState(const uint256& swap_id,
                                          const std::array<uint8_t, 32>& view_key,
                                          PrivateSwapData::SwapState new_state) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_swaps.find(swap_id);
    if (it == m_swaps.end()) {
        return false;
    }

    PrivateSwapData swap;
    if (!EncryptedSwapAnchor::Decrypt(it->second, view_key, swap)) {
        return false;
    }

    swap.state = new_state;

    // Re-encrypt and store
    auto encrypted = EncryptedSwapAnchor::Create(swap, view_key);
    m_swaps[swap_id] = encrypted;

    if (new_state == PrivateSwapData::SwapState::CLAIMED ||
        new_state == PrivateSwapData::SwapState::REFUNDED ||
        new_state == PrivateSwapData::SwapState::EXPIRED) {
        m_active_swaps--;
    }

    LogPrintf("PrivateSwap: Updated swap %s state to %d\n",
              swap_id.GetHex().substr(0, 16), static_cast<int>(new_state));

    return true;
}

bool PrivateSwapManager::RecordEVMTransaction(const uint256& swap_id,
                                               const std::array<uint8_t, 32>& view_key,
                                               const uint256& tx_hash,
                                               const uint256& state_root,
                                               const std::vector<uint8_t>& receipt) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_swaps.find(swap_id);
    if (it == m_swaps.end()) {
        return false;
    }

    PrivateSwapData swap;
    if (!EncryptedSwapAnchor::Decrypt(it->second, view_key, swap)) {
        return false;
    }

    swap.evm_tx_hash = tx_hash;
    swap.evm_state_root = state_root;
    swap.evm_receipt = receipt;

    // Re-encrypt and store
    auto encrypted = EncryptedSwapAnchor::Create(swap, view_key);
    m_swaps[swap_id] = encrypted;

    LogPrintf("PrivateSwap: Recorded EVM tx %s for swap %s\n",
              tx_hash.GetHex().substr(0, 16),
              swap_id.GetHex().substr(0, 16));

    return true;
}

std::vector<uint8_t> PrivateSwapManager::BuildSwapAnchorTag(const PrivateSwapData& data,
                                                             const std::array<uint8_t, 32>& view_key) {
    auto encrypted = EncryptedSwapAnchor::Create(data, view_key);
    auto anchor_bytes = encrypted.Serialize();

    std::vector<uint8_t> tag;

    // TX_EXTRA_NONCE format
    tag.push_back(0x02);  // TX_EXTRA_NONCE
    tag.push_back(static_cast<uint8_t>(anchor_bytes.size() + 1));
    tag.push_back(PRIVATE_SWAP_TAG);
    tag.insert(tag.end(), anchor_bytes.begin(), anchor_bytes.end());

    return tag;
}

bool PrivateSwapManager::ParseSwapAnchor(const std::vector<uint8_t>& extra,
                                          const std::array<uint8_t, 32>& view_key,
                                          PrivateSwapData& out) {
    // Search for private swap tag in extra field
    for (size_t i = 0; i + 3 < extra.size(); i++) {
        if (extra[i] == 0x02) {
            size_t len = extra[i + 1];
            if (i + 2 + len <= extra.size() && extra[i + 2] == PRIVATE_SWAP_TAG) {
                std::vector<uint8_t> anchor_data(extra.begin() + i + 3,
                                                  extra.begin() + i + 2 + len);

                EncryptedSwapAnchor encrypted;
                if (EncryptedSwapAnchor::Deserialize(anchor_data, encrypted)) {
                    if (EncryptedSwapAnchor::Decrypt(encrypted, view_key, out)) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

std::vector<PrivateSwapData> PrivateSwapManager::GetSwapsForViewKey(
    const std::array<uint8_t, 32>& view_key) {

    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PrivateSwapData> result;

    for (const auto& [id, encrypted] : m_swaps) {
        PrivateSwapData swap;
        if (EncryptedSwapAnchor::Decrypt(encrypted, view_key, swap)) {
            result.push_back(swap);
        }
    }

    return result;
}

uint256 PrivateSwapManager::GenerateHashLock(const uint256& preimage) {
    uint256 hash_lock;
    CSHA256 hasher;
    hasher.Write(preimage.data(), 32);
    hasher.Finalize(hash_lock.data());
    return hash_lock;
}

std::vector<uint8_t> PrivateSwapManager::EncryptWithViewKey(const std::vector<uint8_t>& data,
                                                             const std::array<uint8_t, 32>& view_key) {
    std::vector<uint8_t> encrypted(data.size());

    CSHA256 key_stream;
    key_stream.Write(view_key.data(), 32);

    uint8_t key_block[32];
    size_t key_pos = 32;

    for (size_t i = 0; i < data.size(); i++) {
        if (key_pos >= 32) {
            key_stream.Finalize(key_block);
            key_stream.Reset();
            key_stream.Write(key_block, 32);
            key_pos = 0;
        }
        encrypted[i] = data[i] ^ key_block[key_pos++];
    }

    return encrypted;
}

std::vector<uint8_t> PrivateSwapManager::DecryptWithViewKey(const std::vector<uint8_t>& encrypted,
                                                             const std::array<uint8_t, 32>& view_key) {
    // XOR encryption is symmetric
    return EncryptWithViewKey(encrypted, view_key);
}

}  // namespace private_swap
