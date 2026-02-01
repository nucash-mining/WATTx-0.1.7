// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_STRATUM_PARENT_CHAIN_BITCOIN_H
#define WATTX_STRATUM_PARENT_CHAIN_BITCOIN_H

#include <stratum/parent_chain_base.h>
#include <arith_uint256.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <uint256.h>

namespace merged_stratum {

/**
 * Bitcoin-style block header (80 bytes)
 * Used by Bitcoin, Bitcoin Cash, Bitcoin SV, and other SHA256d chains
 */
class BitcoinBlockHeader : public IParentBlockHeader {
public:
    int32_t nVersion{0};
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime{0};
    uint32_t nBits{0};
    uint32_t nNonce{0};

    uint256 GetHash() const override {
        // SHA256d of the 80-byte header
        std::vector<uint8_t> data = Serialize();
        return Hash(data);
    }

    uint256 GetPoWHash() const override {
        // For Bitcoin, PoW hash is same as block hash (SHA256d)
        return GetHash();
    }

    std::vector<uint8_t> Serialize() const override {
        std::vector<uint8_t> data;
        data.reserve(80);

        // Version (4 bytes, little-endian)
        data.push_back(nVersion & 0xFF);
        data.push_back((nVersion >> 8) & 0xFF);
        data.push_back((nVersion >> 16) & 0xFF);
        data.push_back((nVersion >> 24) & 0xFF);

        // Previous block hash (32 bytes)
        data.insert(data.end(), hashPrevBlock.begin(), hashPrevBlock.end());

        // Merkle root (32 bytes)
        data.insert(data.end(), hashMerkleRoot.begin(), hashMerkleRoot.end());

        // Time (4 bytes, little-endian)
        data.push_back(nTime & 0xFF);
        data.push_back((nTime >> 8) & 0xFF);
        data.push_back((nTime >> 16) & 0xFF);
        data.push_back((nTime >> 24) & 0xFF);

        // Bits (4 bytes, little-endian)
        data.push_back(nBits & 0xFF);
        data.push_back((nBits >> 8) & 0xFF);
        data.push_back((nBits >> 16) & 0xFF);
        data.push_back((nBits >> 24) & 0xFF);

        // Nonce (4 bytes, little-endian)
        data.push_back(nNonce & 0xFF);
        data.push_back((nNonce >> 8) & 0xFF);
        data.push_back((nNonce >> 16) & 0xFF);
        data.push_back((nNonce >> 24) & 0xFF);

        return data;
    }

    uint32_t GetNonce() const override { return nNonce; }
    void SetNonce(uint32_t nonce) override { nNonce = nonce; }

    static BitcoinBlockHeader Deserialize(const std::vector<uint8_t>& data) {
        BitcoinBlockHeader header;
        if (data.size() < 80) return header;

        size_t pos = 0;

        // Version
        header.nVersion = data[pos] | (data[pos+1] << 8) | (data[pos+2] << 16) | (data[pos+3] << 24);
        pos += 4;

        // Previous block hash
        std::memcpy(header.hashPrevBlock.data(), &data[pos], 32);
        pos += 32;

        // Merkle root
        std::memcpy(header.hashMerkleRoot.data(), &data[pos], 32);
        pos += 32;

        // Time
        header.nTime = data[pos] | (data[pos+1] << 8) | (data[pos+2] << 16) | (data[pos+3] << 24);
        pos += 4;

        // Bits
        header.nBits = data[pos] | (data[pos+1] << 8) | (data[pos+2] << 16) | (data[pos+3] << 24);
        pos += 4;

        // Nonce
        header.nNonce = data[pos] | (data[pos+1] << 8) | (data[pos+2] << 16) | (data[pos+3] << 24);

        return header;
    }
};

/**
 * Bitcoin/SHA256d parent chain handler
 * Supports Bitcoin, Bitcoin Cash, Bitcoin SV, and similar chains
 */
class BitcoinChainHandler : public ParentChainHandlerBase {
public:
    explicit BitcoinChainHandler(const ParentChainConfig& config)
        : ParentChainHandlerBase(config) {}

    bool GetBlockTemplate(
        std::string& hashing_blob,
        std::string& full_template,
        std::string& seed_hash,
        uint64_t& height,
        uint64_t& difficulty,
        ParentCoinbaseData& coinbase_data
    ) override {
        // Bitcoin uses getblocktemplate RPC
        std::string response = JsonRpcCall("getblocktemplate",
            "[{\"rules\":[\"segwit\"],\"capabilities\":[\"coinbasetxn\",\"workid\",\"coinbase/append\"]}]");

        if (response.empty()) {
            LogPrintf("BitcoinChain: Failed to get block template\n");
            return false;
        }

        // Parse response
        std::string version_str = ParseJsonString(response, "version");
        std::string prevhash = ParseJsonString(response, "previousblockhash");
        std::string bits_str = ParseJsonString(response, "bits");
        std::string height_str = ParseJsonString(response, "height");
        std::string target_str = ParseJsonString(response, "target");
        std::string curtime_str = ParseJsonString(response, "curtime");
        std::string coinbasetxn = ParseJsonString(response, "coinbasetxn");

        if (prevhash.empty()) {
            LogPrintf("BitcoinChain: Invalid block template response\n");
            return false;
        }

        height = height_str.empty() ? 0 : std::stoull(height_str);

        // Parse coinbase transaction
        // Bitcoin's getblocktemplate returns coinbasetxn as hex
        std::string coinbase_hex = ParseJsonString(coinbasetxn.empty() ? response : coinbasetxn, "data");
        if (coinbase_hex.empty()) {
            // Try alternative field names
            coinbase_hex = ParseJsonString(response, "coinbase");
        }

        if (!coinbase_hex.empty()) {
            coinbase_data.coinbase_tx = ParseHex(coinbase_hex);
        }

        // Build header for hashing
        BitcoinBlockHeader header;
        header.nVersion = version_str.empty() ? 0x20000000 : std::stoi(version_str);
        header.hashPrevBlock = uint256::FromHex(prevhash).value_or(uint256{});
        header.nTime = curtime_str.empty() ? GetTime() : std::stoul(curtime_str);
        header.nBits = bits_str.empty() ? 0 : std::stoul(bits_str, nullptr, 16);
        header.nNonce = 0;

        // Store parsed data
        m_current_header = header;
        m_current_prevhash = prevhash;
        m_current_bits = bits_str;

        // Calculate difficulty from target
        if (!target_str.empty()) {
            uint256 target = uint256::FromHex(target_str).value_or(uint256{});
            // difficulty = max_target / target
            arith_uint256 target_arith = UintToArith256(target);
            if (target_arith > 0) {
                arith_uint256 max_target;
                max_target.SetCompact(0x1d00ffff);  // Bitcoin max target
                difficulty = (max_target / target_arith).GetLow64();
            } else {
                difficulty = 1;
            }
        }

        // Build hashing blob (80-byte header)
        auto header_data = header.Serialize();
        hashing_blob = HexStr(header_data);

        full_template = response;
        seed_hash = "";  // Not used for SHA256d

        LogPrintf("BitcoinChain: Got template at height %lu\n", height);
        return true;
    }

    bool ParseBlockTemplate(
        const std::string& template_blob,
        ParentCoinbaseData& coinbase_data
    ) override {
        std::vector<uint8_t> data = ParseHex(template_blob);
        if (data.size() < 80) return false;

        // For Bitcoin, we need to parse the block structure:
        // - 80 byte header
        // - varint tx count
        // - transactions (first is coinbase)

        size_t pos = 80;  // Skip header

        // Transaction count
        uint64_t tx_count;
        pos += ReadVarint(data, pos, tx_count);

        if (tx_count == 0) return false;

        // Parse coinbase transaction
        size_t coinbase_start = pos;

        // TX: version (4), inputs, outputs, locktime (4)
        pos += 4;  // version

        // Check for witness marker
        bool has_witness = false;
        if (pos + 2 <= data.size() && data[pos] == 0x00 && data[pos+1] == 0x01) {
            has_witness = true;
            pos += 2;
        }

        // Input count
        uint64_t vin_count;
        pos += ReadVarint(data, pos, vin_count);

        // Skip inputs
        for (uint64_t i = 0; i < vin_count; i++) {
            pos += 36;  // prevout (32 + 4)
            uint64_t script_len;
            pos += ReadVarint(data, pos, script_len);
            coinbase_data.reserve_offset = pos;  // scriptSig is where MM tag goes
            coinbase_data.reserve_size = script_len;
            pos += script_len;  // scriptSig
            pos += 4;  // sequence
        }

        // Output count
        uint64_t vout_count;
        pos += ReadVarint(data, pos, vout_count);

        // Skip outputs
        for (uint64_t i = 0; i < vout_count; i++) {
            pos += 8;  // value
            uint64_t script_len;
            pos += ReadVarint(data, pos, script_len);
            pos += script_len;  // scriptPubKey
        }

        // Skip witness data if present
        if (has_witness) {
            for (uint64_t i = 0; i < vin_count; i++) {
                uint64_t witness_count;
                pos += ReadVarint(data, pos, witness_count);
                for (uint64_t j = 0; j < witness_count; j++) {
                    uint64_t item_len;
                    pos += ReadVarint(data, pos, item_len);
                    pos += item_len;
                }
            }
        }

        pos += 4;  // locktime

        // Store coinbase
        coinbase_data.coinbase_tx.assign(data.begin() + coinbase_start, data.begin() + pos);
        coinbase_data.coinbase_index = 0;

        // Parse remaining transactions for merkle tree
        std::vector<uint256> tx_hashes;
        tx_hashes.push_back(Hash(coinbase_data.coinbase_tx));

        while (pos < data.size()) {
            size_t tx_start = pos;
            // Simplified: just hash remaining data as one tx
            // In production, properly parse each transaction
            uint256 tx_hash = Hash(std::span<const uint8_t>(data.data() + tx_start, data.size() - tx_start));
            tx_hashes.push_back(tx_hash);
            break;  // Simplified
        }

        coinbase_data.merkle_branch = BuildMerkleBranch(tx_hashes, 0);
        coinbase_data.merkle_root = CalculateMerkleRoot(tx_hashes);

        return true;
    }

    std::string BuildHashingBlob(
        const ParentCoinbaseData& coinbase_data,
        const std::vector<uint8_t>& merge_mining_tag
    ) override {
        // For Bitcoin, the merge mining tag goes in the coinbase scriptSig
        // We need to recalculate the merkle root after modifying coinbase

        std::vector<uint8_t> modified_coinbase = coinbase_data.coinbase_tx;

        // Inject merge mining tag at reserve offset
        if (coinbase_data.reserve_offset > 0 &&
            coinbase_data.reserve_offset + merge_mining_tag.size() <= modified_coinbase.size()) {
            std::memcpy(&modified_coinbase[coinbase_data.reserve_offset],
                       merge_mining_tag.data(),
                       merge_mining_tag.size());
        }

        // Recalculate coinbase hash
        uint256 new_coinbase_hash = Hash(modified_coinbase);

        // Rebuild merkle root
        uint256 new_merkle_root = new_coinbase_hash;
        int idx = 0;
        for (const auto& branch_hash : coinbase_data.merkle_branch) {
            if (idx & 1) {
                new_merkle_root = Hash(branch_hash, new_merkle_root);
            } else {
                new_merkle_root = Hash(new_merkle_root, branch_hash);
            }
            idx >>= 1;
        }

        // Build new header with updated merkle root
        BitcoinBlockHeader header = m_current_header;
        header.hashMerkleRoot = new_merkle_root;

        return HexStr(header.Serialize());
    }

    uint256 CalculatePoWHash(
        const std::vector<uint8_t>& hashing_blob,
        const std::string& /* seed_hash */
    ) override {
        // SHA256d
        return Hash(hashing_blob);
    }

    std::unique_ptr<IParentBlockHeader> BuildBlockHeader(
        const ParentCoinbaseData& coinbase_data,
        uint32_t nonce
    ) override {
        auto header = std::make_unique<BitcoinBlockHeader>(m_current_header);
        header->hashMerkleRoot = coinbase_data.merkle_root;
        header->nNonce = nonce;
        return header;
    }

    bool SubmitBlock(const std::string& block_blob) override {
        std::string response = JsonRpcCall("submitblock", "[\"" + block_blob + "\"]");
        // Bitcoin returns null on success
        return response.find("\"result\":null") != std::string::npos ||
               response.find("\"result\": null") != std::string::npos;
    }

    CAuxPow CreateAuxPow(
        const CBlockHeader& wattx_header,
        const ParentCoinbaseData& coinbase_data,
        uint32_t nonce,
        const std::vector<uint8_t>& merge_mining_tag
    ) override {
        CAuxPow proof;

        // Build parent block header
        BitcoinBlockHeader parent_header = m_current_header;
        parent_header.hashMerkleRoot = coinbase_data.merkle_root;
        parent_header.nNonce = nonce;

        // Convert to CMoneroBlockHeader format (reuse for all parent chains)
        proof.parentBlock.major_version = (parent_header.nVersion >> 24) & 0xFF;
        proof.parentBlock.minor_version = (parent_header.nVersion >> 16) & 0xFF;
        proof.parentBlock.timestamp = parent_header.nTime;
        proof.parentBlock.prev_id = parent_header.hashPrevBlock;
        proof.parentBlock.nonce = parent_header.nNonce;
        proof.parentBlock.merkle_root = parent_header.hashMerkleRoot;

        // Build coinbase transaction with MM tag
        CMutableTransaction coinbase_tx;
        coinbase_tx.version = 2;

        CTxIn coinbase_in;
        coinbase_in.prevout.SetNull();

        // scriptSig contains height + merge mining tag
        std::vector<uint8_t> scriptSig_data;
        scriptSig_data.push_back(0x03);  // Push 3 bytes for height
        uint64_t height = m_current_height;
        scriptSig_data.push_back(height & 0xFF);
        scriptSig_data.push_back((height >> 8) & 0xFF);
        scriptSig_data.push_back((height >> 16) & 0xFF);
        scriptSig_data.insert(scriptSig_data.end(), merge_mining_tag.begin(), merge_mining_tag.end());

        coinbase_in.scriptSig = CScript(scriptSig_data.begin(), scriptSig_data.end());
        coinbase_tx.vin.push_back(coinbase_in);

        CTxOut coinbase_out;
        coinbase_out.nValue = 0;
        coinbase_tx.vout.push_back(coinbase_out);

        proof.coinbaseTxMut = coinbase_tx;
        proof.coinbaseBranch.vHash = coinbase_data.merkle_branch;
        proof.coinbaseBranch.nIndex = 0;
        proof.nChainId = m_config.chain_id;

        return proof;
    }

    uint256 DifficultyToTarget(uint64_t difficulty) override {
        // Bitcoin: target = max_target / difficulty
        // max_target = 0x00000000FFFF0000...
        if (difficulty == 0) difficulty = 1;

        arith_uint256 max_target;
        max_target.SetCompact(0x1d00ffff);  // Bitcoin's max target
        arith_uint256 target = max_target / difficulty;
        return ArithToUint256(target);
    }

private:
    BitcoinBlockHeader m_current_header;
    std::string m_current_prevhash;
    std::string m_current_bits;
    uint64_t m_current_height{0};
};

}  // namespace merged_stratum

#endif  // WATTX_STRATUM_PARENT_CHAIN_BITCOIN_H
