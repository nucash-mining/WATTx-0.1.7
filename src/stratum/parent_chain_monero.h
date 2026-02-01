// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_STRATUM_PARENT_CHAIN_MONERO_H
#define WATTX_STRATUM_PARENT_CHAIN_MONERO_H

#include <stratum/parent_chain_base.h>
#include <arith_uint256.h>
#include <node/randomx_miner.h>

namespace merged_stratum {

/**
 * Monero block header for RandomX PoW
 */
class MoneroBlockHeader : public IParentBlockHeader {
public:
    uint8_t major_version{0};
    uint8_t minor_version{0};
    uint64_t timestamp{0};
    uint256 prev_id;
    uint32_t nonce{0};
    uint256 merkle_root;

    uint256 GetHash() const override {
        // Monero block ID (blob hash)
        std::vector<uint8_t> data = Serialize();
        return Hash(data);
    }

    uint256 GetPoWHash() const override {
        // RandomX hash of the hashing blob
        std::vector<uint8_t> blob = BuildHashingBlob();

        uint256 hash;
        auto& miner = node::GetRandomXMiner();
        if (miner.IsInitialized()) {
            miner.CalculateHash(blob.data(), blob.size(), hash.data());
        } else {
            hash = Hash(blob);
            LogPrintf("MoneroChain: RandomX not initialized, using SHA256d fallback\n");
        }

        return hash;
    }

    std::vector<uint8_t> BuildHashingBlob() const {
        std::vector<uint8_t> blob;
        blob.reserve(76);

        // Major version
        blob.push_back(major_version);

        // Minor version
        blob.push_back(minor_version);

        // Timestamp as varint
        uint64_t ts = timestamp;
        while (ts >= 0x80) {
            blob.push_back((ts & 0x7F) | 0x80);
            ts >>= 7;
        }
        blob.push_back(static_cast<uint8_t>(ts));

        // Previous block hash
        blob.insert(blob.end(), prev_id.begin(), prev_id.end());

        // Nonce (4 bytes, little-endian)
        blob.push_back((nonce >> 0) & 0xFF);
        blob.push_back((nonce >> 8) & 0xFF);
        blob.push_back((nonce >> 16) & 0xFF);
        blob.push_back((nonce >> 24) & 0xFF);

        // Tree root (merkle_root)
        blob.insert(blob.end(), merkle_root.begin(), merkle_root.end());

        // Pad to 76 bytes
        while (blob.size() < 76) {
            blob.push_back(0);
        }

        return blob;
    }

    std::vector<uint8_t> Serialize() const override {
        return BuildHashingBlob();
    }

    uint32_t GetNonce() const override { return nonce; }
    void SetNonce(uint32_t n) override { nonce = n; }
};

/**
 * Monero/RandomX parent chain handler
 */
class MoneroChainHandler : public ParentChainHandlerBase {
public:
    explicit MoneroChainHandler(const ParentChainConfig& config)
        : ParentChainHandlerBase(config) {}

    bool GetBlockTemplate(
        std::string& hashing_blob,
        std::string& full_template,
        std::string& seed_hash,
        uint64_t& height,
        uint64_t& difficulty,
        ParentCoinbaseData& coinbase_data
    ) override {
        // Reserve 194 bytes for merge mining tag + EVM anchor
        std::ostringstream params;
        params << "{\"wallet_address\":\"" << m_config.wallet_address << "\",\"reserve_size\":194}";

        std::string response = HttpPost("/json_rpc",
            "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"get_block_template\",\"params\":" + params.str() + "}");

        if (response.empty()) {
            LogPrintf("MoneroChain: Failed to get block template\n");
            return false;
        }

        // Parse response
        hashing_blob = ParseJsonString(response, "blockhashing_blob");
        full_template = ParseJsonString(response, "blocktemplate_blob");
        seed_hash = ParseJsonString(response, "seed_hash");

        std::string height_str = ParseJsonString(response, "height");
        std::string diff_str = ParseJsonString(response, "difficulty");
        std::string reserve_offset_str = ParseJsonString(response, "reserved_offset");

        if (full_template.empty()) {
            LogPrintf("MoneroChain: Invalid block template response\n");
            return false;
        }

        height = height_str.empty() ? 0 : std::stoull(height_str);
        difficulty = diff_str.empty() ? 0 : std::stoull(diff_str);
        m_current_height = height;
        m_seed_hash = seed_hash;

        // Parse the full block template
        if (!ParseBlockTemplate(full_template, coinbase_data)) {
            LogPrintf("MoneroChain: Failed to parse block template\n");
            return false;
        }

        coinbase_data.reserve_offset = reserve_offset_str.empty() ? 0 : std::stoull(reserve_offset_str);
        coinbase_data.reserve_size = 194;

        LogPrintf("MoneroChain: Got template at height %lu, difficulty %lu\n", height, difficulty);
        return true;
    }

    bool ParseBlockTemplate(
        const std::string& template_blob,
        ParentCoinbaseData& coinbase_data
    ) override {
        std::vector<uint8_t> blob = ParseHex(template_blob);
        if (blob.size() < 100) return false;

        size_t pos = 0;
        uint64_t temp;

        // Parse block header
        pos += ReadVarint(blob, pos, temp);
        m_current_header.major_version = static_cast<uint8_t>(temp);

        pos += ReadVarint(blob, pos, temp);
        m_current_header.minor_version = static_cast<uint8_t>(temp);

        pos += ReadVarint(blob, pos, m_current_header.timestamp);

        if (pos + 32 > blob.size()) return false;
        std::memcpy(m_current_header.prev_id.data(), &blob[pos], 32);
        pos += 32;

        if (pos + 4 > blob.size()) return false;
        m_current_header.nonce = blob[pos] | (blob[pos+1] << 8) | (blob[pos+2] << 16) | (blob[pos+3] << 24);
        pos += 4;

        // Record coinbase start
        size_t coinbase_start = pos;

        // Parse coinbase transaction (simplified)
        pos += ReadVarint(blob, pos, temp);  // version
        pos += ReadVarint(blob, pos, temp);  // unlock_time

        uint64_t vin_count;
        pos += ReadVarint(blob, pos, vin_count);

        for (uint64_t i = 0; i < vin_count && pos < blob.size(); i++) {
            uint8_t input_type = blob[pos++];
            if (input_type == 0xff) {
                pos += ReadVarint(blob, pos, temp);  // height
            }
        }

        uint64_t vout_count;
        pos += ReadVarint(blob, pos, vout_count);

        for (uint64_t i = 0; i < vout_count && pos < blob.size(); i++) {
            pos += ReadVarint(blob, pos, temp);  // amount
            if (pos < blob.size()) {
                uint8_t out_type = blob[pos++];
                if (out_type == 2) pos += 32;
                else if (out_type == 3) pos += 33;
                else pos += 32;
            }
        }

        // Extra field
        uint64_t extra_len;
        pos += ReadVarint(blob, pos, extra_len);
        coinbase_data.reserve_offset = pos;
        coinbase_data.reserve_size = extra_len;
        pos += extra_len;

        size_t coinbase_end = pos;
        coinbase_data.coinbase_tx.assign(blob.begin() + coinbase_start, blob.begin() + coinbase_end);

        // Parse transaction hashes for merkle tree
        uint64_t tx_hash_count;
        pos += ReadVarint(blob, pos, tx_hash_count);

        std::vector<uint256> tx_hashes;
        tx_hashes.push_back(Hash(coinbase_data.coinbase_tx));

        for (uint64_t i = 0; i < tx_hash_count && pos + 32 <= blob.size(); i++) {
            uint256 tx_hash;
            std::memcpy(tx_hash.data(), &blob[pos], 32);
            tx_hashes.push_back(tx_hash);
            pos += 32;
        }

        coinbase_data.coinbase_index = 0;
        coinbase_data.merkle_branch = BuildMerkleBranch(tx_hashes, 0);
        coinbase_data.merkle_root = CalculateMerkleRoot(tx_hashes);

        m_current_header.merkle_root = coinbase_data.merkle_root;

        return true;
    }

    std::string BuildHashingBlob(
        const ParentCoinbaseData& coinbase_data,
        const std::vector<uint8_t>& merge_mining_tag
    ) override {
        // Modify coinbase with merge mining tag
        std::vector<uint8_t> modified_coinbase = coinbase_data.coinbase_tx;

        if (coinbase_data.reserve_offset > 0 &&
            coinbase_data.reserve_offset + merge_mining_tag.size() <= modified_coinbase.size()) {

            // Find zeros in extra field and inject tag
            size_t inject_pos = 0;

            // Re-parse to find extra field position within coinbase
            size_t pos = 0;
            uint64_t temp;
            pos += ReadVarint(modified_coinbase, pos, temp);  // version
            pos += ReadVarint(modified_coinbase, pos, temp);  // unlock_time

            uint64_t vin_count;
            pos += ReadVarint(modified_coinbase, pos, vin_count);
            for (uint64_t i = 0; i < vin_count && pos < modified_coinbase.size(); i++) {
                uint8_t input_type = modified_coinbase[pos++];
                if (input_type == 0xff) {
                    pos += ReadVarint(modified_coinbase, pos, temp);
                }
            }

            uint64_t vout_count;
            pos += ReadVarint(modified_coinbase, pos, vout_count);
            for (uint64_t i = 0; i < vout_count && pos < modified_coinbase.size(); i++) {
                pos += ReadVarint(modified_coinbase, pos, temp);
                if (pos < modified_coinbase.size()) {
                    uint8_t out_type = modified_coinbase[pos++];
                    if (out_type == 2) pos += 32;
                    else if (out_type == 3) pos += 33;
                    else pos += 32;
                }
            }

            uint64_t extra_len;
            pos += ReadVarint(modified_coinbase, pos, extra_len);
            size_t extra_start = pos;

            // Find first zero byte in extra (reserved space)
            for (size_t i = extra_start; i < extra_start + extra_len && i < modified_coinbase.size(); i++) {
                if (modified_coinbase[i] == 0) {
                    inject_pos = i;
                    break;
                }
            }

            if (inject_pos > 0 && inject_pos + merge_mining_tag.size() <= modified_coinbase.size()) {
                std::memcpy(&modified_coinbase[inject_pos],
                           merge_mining_tag.data(),
                           merge_mining_tag.size());
            }
        }

        // Recalculate merkle root
        uint256 new_coinbase_hash = Hash(modified_coinbase);
        uint256 new_merkle_root = new_coinbase_hash;

        int idx = 0;
        for (const auto& branch_hash : coinbase_data.merkle_branch) {
            if (idx & 1) {
                std::vector<uint8_t> combined;
                combined.insert(combined.end(), branch_hash.begin(), branch_hash.end());
                combined.insert(combined.end(), new_merkle_root.begin(), new_merkle_root.end());
                new_merkle_root = Hash(combined);
            } else {
                std::vector<uint8_t> combined;
                combined.insert(combined.end(), new_merkle_root.begin(), new_merkle_root.end());
                combined.insert(combined.end(), branch_hash.begin(), branch_hash.end());
                new_merkle_root = Hash(combined);
            }
            idx >>= 1;
        }

        // Build new hashing blob
        MoneroBlockHeader header = m_current_header;
        header.merkle_root = new_merkle_root;

        return HexStr(header.BuildHashingBlob());
    }

    uint256 CalculatePoWHash(
        const std::vector<uint8_t>& hashing_blob,
        const std::string& seed_hash
    ) override {
        uint256 hash;
        auto& miner = node::GetRandomXMiner();

        // Initialize RandomX with seed if needed
        if (!seed_hash.empty() && miner.IsInitialized()) {
            // TODO: handle seed changes
        }

        if (miner.IsInitialized()) {
            miner.CalculateHash(hashing_blob.data(), hashing_blob.size(), hash.data());
        } else {
            hash = Hash(hashing_blob);
        }

        return hash;
    }

    std::unique_ptr<IParentBlockHeader> BuildBlockHeader(
        const ParentCoinbaseData& coinbase_data,
        uint32_t nonce
    ) override {
        auto header = std::make_unique<MoneroBlockHeader>(m_current_header);
        header->merkle_root = coinbase_data.merkle_root;
        header->nonce = nonce;
        return header;
    }

    bool SubmitBlock(const std::string& block_blob) override {
        std::string response = HttpPost("/json_rpc",
            "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"submit_block\",\"params\":[\"" + block_blob + "\"]}");

        return response.find("\"status\":\"OK\"") != std::string::npos;
    }

    CAuxPow CreateAuxPow(
        const CBlockHeader& wattx_header,
        const ParentCoinbaseData& coinbase_data,
        uint32_t nonce,
        const std::vector<uint8_t>& merge_mining_tag
    ) override {
        CAuxPow proof;

        MoneroBlockHeader parent_header = m_current_header;
        parent_header.merkle_root = coinbase_data.merkle_root;
        parent_header.nonce = nonce;

        // Convert to CMoneroBlockHeader format
        proof.parentBlock.major_version = parent_header.major_version;
        proof.parentBlock.minor_version = parent_header.minor_version;
        proof.parentBlock.timestamp = parent_header.timestamp;
        proof.parentBlock.prev_id = parent_header.prev_id;
        proof.parentBlock.nonce = parent_header.nonce;
        proof.parentBlock.merkle_root = parent_header.merkle_root;

        // Build coinbase with merge mining tag
        CMutableTransaction coinbase_tx;
        coinbase_tx.version = 2;

        CTxIn coinbase_in;
        coinbase_in.prevout.SetNull();

        std::vector<uint8_t> scriptSig_data;
        scriptSig_data.push_back(0x03);
        scriptSig_data.push_back(m_current_height & 0xFF);
        scriptSig_data.push_back((m_current_height >> 8) & 0xFF);
        scriptSig_data.push_back((m_current_height >> 16) & 0xFF);
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
        if (difficulty == 0) difficulty = 1;

        // Monero: target = (2^256 - 1) / difficulty
        uint256 max_uint256;
        std::memset(max_uint256.data(), 0xff, 32);
        arith_uint256 max_target = UintToArith256(max_uint256);
        arith_uint256 target = max_target / difficulty;
        return ArithToUint256(target);
    }

private:
    MoneroBlockHeader m_current_header;
    uint64_t m_current_height{0};
    std::string m_seed_hash;
};

}  // namespace merged_stratum

#endif  // WATTX_STRATUM_PARENT_CHAIN_MONERO_H
