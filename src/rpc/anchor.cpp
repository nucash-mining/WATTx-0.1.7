// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <anchor/evm_anchor.h>
#include <anchor/private_swap.h>
#include <auxpow/auxpow.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <core_io.h>
#include <node/blockstorage.h>
#include <node/context.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <script/script.h>
#include <stratum/merged_stratum.h>
#include <stratum/mining_rewards.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <validation.h>

#include <stdexcept>

using node::NodeContext;

static RPCHelpMan getevmanchorinfo()
{
    return RPCHelpMan{"getevmanchorinfo",
        "\nReturns information about EVM transaction anchoring status.\n",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "active", "Whether EVM anchoring is currently active"},
                {RPCResult::Type::NUM, "activation_height", "Block height at which anchoring activates"},
                {RPCResult::Type::NUM, "current_height", "Current blockchain height"},
                {RPCResult::Type::NUM, "total_anchors", "Total number of anchors created"},
                {RPCResult::Type::NUM, "total_evm_tx_anchored", "Total EVM transactions anchored"},
                {RPCResult::Type::STR_HEX, "view_public_key", "Public view key for anchor verification"},
            }},
        RPCExamples{
            HelpExampleCli("getevmanchorinfo", "")
            + HelpExampleRpc("getevmanchorinfo", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            const CChainParams& chainparams = Params();
            auto& anchor_mgr = evm_anchor::GetEVMAnchorManager();
            ChainstateManager& chainman = EnsureAnyChainman(request.context);

            int current_height = 0;
            {
                LOCK(cs_main);
                current_height = chainman.ActiveChain().Height();
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("active", anchor_mgr.IsActive(current_height));
            result.pushKV("activation_height", anchor_mgr.GetActivationHeight());
            result.pushKV("current_height", current_height);
            result.pushKV("total_anchors", (uint64_t)anchor_mgr.GetTotalAnchors());
            result.pushKV("total_evm_tx_anchored", (uint64_t)anchor_mgr.GetTotalEVMTxAnchored());

            auto view_key = anchor_mgr.GetViewPublicKey();
            result.pushKV("view_public_key", HexStr(view_key));

            return result;
        },
    };
}

static RPCHelpMan getevmanchor()
{
    return RPCHelpMan{"getevmanchor",
        "\nGet EVM anchor data for a specific block.\n",
        {
            {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The block hash"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::NUM, "version", "Anchor data version"},
                {RPCResult::Type::NUM, "wattx_block_height", "WATTx block height"},
                {RPCResult::Type::NUM, "evm_tx_count", "Number of EVM transactions in anchor"},
                {RPCResult::Type::STR_HEX, "evm_merkle_root", "Merkle root of EVM transaction hashes"},
                {RPCResult::Type::STR_HEX, "state_root", "EVM state root"},
                {RPCResult::Type::STR_HEX, "utxo_root", "UTXO root"},
                {RPCResult::Type::NUM, "timestamp", "Block timestamp"},
                {RPCResult::Type::STR_HEX, "anchor_hash", "Unique anchor identifier"},
                {RPCResult::Type::BOOL, "valid", "Whether anchor data is valid"},
            }},
        RPCExamples{
            HelpExampleCli("getevmanchor", "\"blockhash\"")
            + HelpExampleRpc("getevmanchor", "\"blockhash\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            uint256 hash = ParseHashV(request.params[0], "blockhash");
            ChainstateManager& chainman = EnsureAnyChainman(request.context);

            CBlockIndex* pblockindex;
            CBlock block;
            {
                LOCK(cs_main);
                pblockindex = chainman.m_blockman.LookupBlockIndex(hash);
                if (!pblockindex) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
                }

                if (!chainman.m_blockman.ReadBlock(block, *pblockindex)) {
                    throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
                }
            }

            auto& anchor_mgr = evm_anchor::GetEVMAnchorManager();

            // Create anchor for this block
            auto anchor = anchor_mgr.CreateAnchor(block, pblockindex->nHeight);

            UniValue result(UniValue::VOBJ);
            result.pushKV("version", (int)anchor.version);
            result.pushKV("wattx_block_height", (int)anchor.wattx_block_height);
            result.pushKV("evm_tx_count", (int)anchor.evm_tx_count);
            result.pushKV("evm_merkle_root", anchor.evm_merkle_root.GetHex());
            result.pushKV("state_root", anchor.state_root.GetHex());
            result.pushKV("utxo_root", anchor.utxo_root.GetHex());
            result.pushKV("timestamp", anchor.timestamp);
            result.pushKV("anchor_hash", anchor.GetHash().GetHex());
            result.pushKV("valid", anchor.IsValid());

            return result;
        },
    };
}

static RPCHelpMan verifyevmanchor()
{
    return RPCHelpMan{"verifyevmanchor",
        "\nVerify an EVM anchor from Monero block extra field.\n",
        {
            {"anchor_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Hex-encoded anchor data from Monero coinbase extra"},
            {"view_key", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Optional view public key (uses default if omitted)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "valid", "Whether anchor is valid and verified with view key"},
                {RPCResult::Type::NUM, "wattx_block_height", "WATTx block height (if valid)"},
                {RPCResult::Type::NUM, "evm_tx_count", "Number of EVM transactions (if valid)"},
                {RPCResult::Type::STR_HEX, "evm_merkle_root", "EVM merkle root (if valid)"},
                {RPCResult::Type::STR, "error", "Error message (if invalid)"},
            }},
        RPCExamples{
            HelpExampleCli("verifyevmanchor", "\"anchor_hex\"")
            + HelpExampleRpc("verifyevmanchor", "\"anchor_hex\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::string anchor_hex = request.params[0].get_str();
            std::vector<uint8_t> anchor_data = ParseHex(anchor_hex);

            auto& anchor_mgr = evm_anchor::GetEVMAnchorManager();

            // Get view key (use provided or default)
            std::array<uint8_t, 32> view_key = anchor_mgr.GetViewPublicKey();
            if (!request.params[1].isNull()) {
                std::vector<uint8_t> key_bytes = ParseHex(request.params[1].get_str());
                if (key_bytes.size() != 32) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "View key must be 32 bytes");
                }
                std::copy(key_bytes.begin(), key_bytes.end(), view_key.begin());
            }

            UniValue result(UniValue::VOBJ);

            // Try to parse and verify anchor
            evm_anchor::EVMAnchorData anchor;
            if (anchor_mgr.ParseAnchorTag(anchor_data, anchor)) {
                result.pushKV("valid", true);
                result.pushKV("wattx_block_height", (int)anchor.wattx_block_height);
                result.pushKV("evm_tx_count", (int)anchor.evm_tx_count);
                result.pushKV("evm_merkle_root", anchor.evm_merkle_root.GetHex());
                result.pushKV("state_root", anchor.state_root.GetHex());
                result.pushKV("utxo_root", anchor.utxo_root.GetHex());
                result.pushKV("timestamp", anchor.timestamp);
            } else {
                result.pushKV("valid", false);
                result.pushKV("error", "Failed to verify anchor with view key");
            }

            return result;
        },
    };
}

static RPCHelpMan getevmtxlist()
{
    return RPCHelpMan{"getevmtxlist",
        "\nGet list of EVM transaction hashes for a block.\n",
        {
            {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The block hash"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::NUM, "height", "Block height"},
                {RPCResult::Type::NUM, "evm_tx_count", "Number of EVM transactions"},
                {RPCResult::Type::STR_HEX, "merkle_root", "Merkle root of EVM tx hashes"},
                {RPCResult::Type::ARR, "transactions", "List of EVM transaction hashes",
                    {
                        {RPCResult::Type::STR_HEX, "", "Transaction hash"},
                    }},
            }},
        RPCExamples{
            HelpExampleCli("getevmtxlist", "\"blockhash\"")
            + HelpExampleRpc("getevmtxlist", "\"blockhash\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            uint256 hash = ParseHashV(request.params[0], "blockhash");
            ChainstateManager& chainman = EnsureAnyChainman(request.context);

            CBlockIndex* pblockindex;
            CBlock block;
            {
                LOCK(cs_main);
                pblockindex = chainman.m_blockman.LookupBlockIndex(hash);
                if (!pblockindex) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
                }

                if (!chainman.m_blockman.ReadBlock(block, *pblockindex)) {
                    throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
                }
            }

            auto& anchor_mgr = evm_anchor::GetEVMAnchorManager();

            // Get EVM transaction hashes
            std::vector<uint256> evm_hashes = anchor_mgr.GetEVMTransactionHashes(block);
            uint256 merkle_root = anchor_mgr.ComputeEVMMerkleRoot(evm_hashes);

            UniValue result(UniValue::VOBJ);
            result.pushKV("height", pblockindex->nHeight);
            result.pushKV("evm_tx_count", (int)evm_hashes.size());
            result.pushKV("merkle_root", merkle_root.GetHex());

            UniValue txlist(UniValue::VARR);
            for (const auto& txhash : evm_hashes) {
                txlist.push_back(txhash.GetHex());
            }
            result.pushKV("transactions", txlist);

            return result;
        },
    };
}

static RPCHelpMan setevmanchoractivation()
{
    return RPCHelpMan{"setevmanchoractivation",
        "\nSet the EVM anchor activation height (for testing).\n",
        {
            {"height", RPCArg::Type::NUM, RPCArg::Optional::NO, "Activation block height"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "success", "Whether activation height was set"},
                {RPCResult::Type::NUM, "activation_height", "New activation height"},
            }},
        RPCExamples{
            HelpExampleCli("setevmanchoractivation", "50000")
            + HelpExampleRpc("setevmanchoractivation", "50000")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            int height = request.params[0].getInt<int>();
            if (height < 0) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Height must be non-negative");
            }

            auto& anchor_mgr = evm_anchor::GetEVMAnchorManager();
            anchor_mgr.SetActivationHeight(height);

            UniValue result(UniValue::VOBJ);
            result.pushKV("success", true);
            result.pushKV("activation_height", height);

            return result;
        },
    };
}

// ============================================================================
// Private Swap RPC Commands
// ============================================================================

static RPCHelpMan initiateswap()
{
    return RPCHelpMan{"initiateswap",
        "\nInitiate a private cross-chain swap.\n",
        {
            {"source_chain", RPCArg::Type::STR, RPCArg::Optional::NO, "Source chain (WATTX_EVM, MONERO, SOLANA, XRP, XPL, ETHEREUM, BSC, POLYGON)"},
            {"source_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Source address"},
            {"source_amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount on source chain"},
            {"source_asset", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Asset/token on source chain (default: native)"},
            {"dest_chain", RPCArg::Type::STR, RPCArg::Optional::NO, "Destination chain"},
            {"dest_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Destination address"},
            {"dest_amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount on destination chain"},
            {"dest_asset", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Asset/token on destination chain (default: native)"},
            {"timelock", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Time lock in seconds (default: 3600)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "swap_id", "Unique swap identifier"},
                {RPCResult::Type::STR_HEX, "view_key", "Private view key for this swap (share with counterparty)"},
                {RPCResult::Type::STR_HEX, "hash_lock", "HTLC hash lock"},
                {RPCResult::Type::NUM, "expires_at", "Expiration timestamp"},
            }},
        RPCExamples{
            HelpExampleCli("initiateswap", "\"WATTX_EVM\" \"Waddr...\" 100 \"\" \"MONERO\" \"4addr...\" 0.5 \"\" 7200")
            + HelpExampleRpc("initiateswap", "\"WATTX_EVM\", \"Waddr...\", 100, \"\", \"MONERO\", \"4addr...\", 0.5, \"\", 7200")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            auto& swap_mgr = private_swap::GetPrivateSwapManager();

            std::string source_chain_str = request.params[0].get_str();
            std::string source_address = request.params[1].get_str();
            uint64_t source_amount = AmountFromValue(request.params[2]);
            std::string source_asset = request.params[3].isNull() ? "" : request.params[3].get_str();

            std::string dest_chain_str = request.params[4].get_str();
            std::string dest_address = request.params[5].get_str();
            uint64_t dest_amount = AmountFromValue(request.params[6]);
            std::string dest_asset = request.params[7].isNull() ? "" : request.params[7].get_str();

            uint64_t timelock = request.params[8].isNull() ? 3600 : request.params[8].getInt<uint64_t>();

            auto source_chain = private_swap::StringToChainType(source_chain_str);
            auto dest_chain = private_swap::StringToChainType(dest_chain_str);

            auto [swap_id, view_key] = swap_mgr.InitiateSwap(
                source_chain, source_address, source_amount, source_asset,
                dest_chain, dest_address, dest_amount, dest_asset,
                timelock);

            private_swap::PrivateSwapData swap;
            swap_mgr.GetSwap(swap_id, view_key, swap);

            UniValue result(UniValue::VOBJ);
            result.pushKV("swap_id", swap_id.GetHex());
            result.pushKV("view_key", HexStr(view_key));
            result.pushKV("hash_lock", swap.hash_lock.GetHex());
            result.pushKV("expires_at", swap.expires_at);

            return result;
        },
    };
}

static RPCHelpMan getswap()
{
    return RPCHelpMan{"getswap",
        "\nGet private swap details (requires view key).\n",
        {
            {"swap_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Swap identifier"},
            {"view_key", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "View key for this swap"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "swap_id", "Swap identifier"},
                {RPCResult::Type::STR, "source_chain", "Source chain"},
                {RPCResult::Type::STR, "source_address", "Source address"},
                {RPCResult::Type::STR_AMOUNT, "source_amount", "Source amount"},
                {RPCResult::Type::STR, "dest_chain", "Destination chain"},
                {RPCResult::Type::STR, "dest_address", "Destination address"},
                {RPCResult::Type::STR_AMOUNT, "dest_amount", "Destination amount"},
                {RPCResult::Type::STR, "state", "Swap state"},
                {RPCResult::Type::NUM, "created_at", "Creation timestamp"},
                {RPCResult::Type::NUM, "expires_at", "Expiration timestamp"},
            }},
        RPCExamples{
            HelpExampleCli("getswap", "\"swap_id\" \"view_key\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            uint256 swap_id = ParseHashV(request.params[0], "swap_id");

            std::vector<uint8_t> key_bytes = ParseHex(request.params[1].get_str());
            if (key_bytes.size() != 32) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "View key must be 32 bytes");
            }
            std::array<uint8_t, 32> view_key;
            std::copy(key_bytes.begin(), key_bytes.end(), view_key.begin());

            auto& swap_mgr = private_swap::GetPrivateSwapManager();

            private_swap::PrivateSwapData swap;
            if (!swap_mgr.GetSwap(swap_id, view_key, swap)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Swap not found or invalid view key");
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("swap_id", swap.swap_id.GetHex());
            result.pushKV("source_chain", private_swap::ChainTypeToString(swap.source_chain));
            result.pushKV("source_address", swap.source_address);
            result.pushKV("source_amount", ValueFromAmount(swap.source_amount));
            result.pushKV("source_asset", swap.source_asset);
            result.pushKV("dest_chain", private_swap::ChainTypeToString(swap.dest_chain));
            result.pushKV("dest_address", swap.dest_address);
            result.pushKV("dest_amount", ValueFromAmount(swap.dest_amount));
            result.pushKV("dest_asset", swap.dest_asset);
            result.pushKV("hash_lock", swap.hash_lock.GetHex());
            result.pushKV("state", static_cast<int>(swap.state));
            result.pushKV("created_at", swap.created_at);
            result.pushKV("expires_at", swap.expires_at);

            if (!swap.evm_tx_hash.IsNull()) {
                result.pushKV("evm_tx_hash", swap.evm_tx_hash.GetHex());
                result.pushKV("evm_state_root", swap.evm_state_root.GetHex());
            }

            return result;
        },
    };
}

static RPCHelpMan joinswap()
{
    return RPCHelpMan{"joinswap",
        "\nJoin a private swap as participant.\n",
        {
            {"swap_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Swap identifier"},
            {"view_key", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "View key shared by initiator"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "success", "Whether join was successful"},
            }},
        RPCExamples{
            HelpExampleCli("joinswap", "\"swap_id\" \"view_key\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            uint256 swap_id = ParseHashV(request.params[0], "swap_id");

            std::vector<uint8_t> key_bytes = ParseHex(request.params[1].get_str());
            if (key_bytes.size() != 32) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "View key must be 32 bytes");
            }
            std::array<uint8_t, 32> view_key;
            std::copy(key_bytes.begin(), key_bytes.end(), view_key.begin());

            auto& swap_mgr = private_swap::GetPrivateSwapManager();

            bool success = swap_mgr.JoinSwap(swap_id, view_key);

            UniValue result(UniValue::VOBJ);
            result.pushKV("success", success);

            return result;
        },
    };
}

static RPCHelpMan buildswapanchortag()
{
    return RPCHelpMan{"buildswapanchortag",
        "\nBuild a private swap anchor tag for Monero coinbase.\n",
        {
            {"swap_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Swap identifier"},
            {"view_key", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "View key for this swap"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "anchor_tag", "Hex-encoded anchor tag for Monero extra field"},
                {RPCResult::Type::NUM, "size", "Size in bytes"},
            }},
        RPCExamples{
            HelpExampleCli("buildswapanchortag", "\"swap_id\" \"view_key\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            uint256 swap_id = ParseHashV(request.params[0], "swap_id");

            std::vector<uint8_t> key_bytes = ParseHex(request.params[1].get_str());
            if (key_bytes.size() != 32) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "View key must be 32 bytes");
            }
            std::array<uint8_t, 32> view_key;
            std::copy(key_bytes.begin(), key_bytes.end(), view_key.begin());

            auto& swap_mgr = private_swap::GetPrivateSwapManager();

            private_swap::PrivateSwapData swap;
            if (!swap_mgr.GetSwap(swap_id, view_key, swap)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Swap not found or invalid view key");
            }

            auto anchor_tag = swap_mgr.BuildSwapAnchorTag(swap, view_key);

            UniValue result(UniValue::VOBJ);
            result.pushKV("anchor_tag", HexStr(anchor_tag));
            result.pushKV("size", (int)anchor_tag.size());

            return result;
        },
    };
}

static RPCHelpMan getswapstats()
{
    return RPCHelpMan{"getswapstats",
        "\nGet private swap statistics.\n",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::NUM, "total_swaps", "Total swaps initiated"},
                {RPCResult::Type::NUM, "active_swaps", "Currently active swaps"},
            }},
        RPCExamples{
            HelpExampleCli("getswapstats", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            auto& swap_mgr = private_swap::GetPrivateSwapManager();

            UniValue result(UniValue::VOBJ);
            result.pushKV("total_swaps", (uint64_t)swap_mgr.GetTotalSwaps());
            result.pushKV("active_swaps", (uint64_t)swap_mgr.GetActiveSwaps());

            return result;
        },
    };
}

// ============================================================================
// Mining Rewards RPC Commands
// ============================================================================

static RPCHelpMan setupminingrewards()
{
    return RPCHelpMan{"setupminingrewards",
        "\nConfigure mining rewards contract for dual mining.\n",
        {
            {"contract_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Mining rewards contract address"},
            {"operator_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Operator wallet address for signing"},
            {"rpc_host", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "WATTx RPC host (default: 127.0.0.1)"},
            {"rpc_port", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "WATTx RPC port (default: 1337)"},
            {"rpc_user", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "RPC username"},
            {"rpc_pass", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "RPC password"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "success", "Whether setup was successful"},
                {RPCResult::Type::STR, "contract_address", "Contract address"},
            }},
        RPCExamples{
            HelpExampleCli("setupminingrewards", "\"0x1234...\" \"Waddr...\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            mining_rewards::MiningRewardsConfig config;
            config.enabled = true;
            config.contract_address = request.params[0].get_str();
            config.operator_address = request.params[1].get_str();

            if (!request.params[2].isNull()) {
                config.wattx_rpc_host = request.params[2].get_str();
            }
            if (!request.params[3].isNull()) {
                config.wattx_rpc_port = request.params[3].getInt<int>();
            }
            if (!request.params[4].isNull()) {
                config.wattx_rpc_user = request.params[4].get_str();
            }
            if (!request.params[5].isNull()) {
                config.wattx_rpc_pass = request.params[5].get_str();
            }

            auto& rewards_mgr = mining_rewards::GetMiningRewardsManager();

            bool success = rewards_mgr.Initialize(config);
            if (success) {
                success = rewards_mgr.Start();
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("success", success);
            result.pushKV("contract_address", config.contract_address);

            return result;
        },
    };
}

static RPCHelpMan getminingrewardsstats()
{
    return RPCHelpMan{"getminingrewardsstats",
        "\nGet mining rewards statistics.\n",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "running", "Whether mining rewards is running"},
                {RPCResult::Type::STR, "contract_address", "Contract address"},
                {RPCResult::Type::NUM, "pending_shares", "Shares pending submission"},
                {RPCResult::Type::NUM, "total_shares_submitted", "Total shares submitted"},
                {RPCResult::Type::NUM, "total_tx_sent", "Total transactions sent"},
                {RPCResult::Type::NUM, "total_blocks_finalized", "Total blocks finalized"},
            }},
        RPCExamples{
            HelpExampleCli("getminingrewardsstats", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            auto& rewards_mgr = mining_rewards::GetMiningRewardsManager();

            UniValue result(UniValue::VOBJ);
            result.pushKV("running", rewards_mgr.IsRunning());
            result.pushKV("contract_address", rewards_mgr.GetContractAddress());
            result.pushKV("pending_shares", (uint64_t)rewards_mgr.GetPendingShareCount());
            result.pushKV("total_shares_submitted", rewards_mgr.GetTotalSharesSubmitted());
            result.pushKV("total_tx_sent", rewards_mgr.GetTotalTxSent());
            result.pushKV("total_blocks_finalized", rewards_mgr.GetTotalBlocksFinalized());

            return result;
        },
    };
}

static RPCHelpMan stopminingrewards()
{
    return RPCHelpMan{"stopminingrewards",
        "\nStop mining rewards submission.\n",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "success", "Whether stop was successful"},
            }},
        RPCExamples{
            HelpExampleCli("stopminingrewards", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            auto& rewards_mgr = mining_rewards::GetMiningRewardsManager();
            rewards_mgr.Stop();

            UniValue result(UniValue::VOBJ);
            result.pushKV("success", true);

            return result;
        },
    };
}

// ============================================================================
// Merged Mining Test RPC Commands
// ============================================================================

static RPCHelpMan testauxpowconstruction()
{
    return RPCHelpMan{"testauxpowconstruction",
        "\nTest AuxPoW proof construction with mock Monero data.\n"
        "This creates a mock Monero block template and tests the full AuxPoW construction pipeline.\n",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "success", "Whether the test passed"},
                {RPCResult::Type::STR_HEX, "wattx_block_hash", "WATTx block header hash"},
                {RPCResult::Type::STR_HEX, "aux_merkle_root", "Auxiliary chain merkle root"},
                {RPCResult::Type::STR_HEX, "merge_mining_tag", "Merge mining tag (hex)"},
                {RPCResult::Type::NUM, "tag_size", "Size of merge mining tag in bytes"},
                {RPCResult::Type::STR, "error", /*optional=*/true, "Error message if failed"},
            }},
        RPCExamples{
            HelpExampleCli("testauxpowconstruction", "")
            + HelpExampleRpc("testauxpowconstruction", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            UniValue result(UniValue::VOBJ);

            try {
                // Create a mock WATTx block header
                CBlockHeader wattx_header;
                wattx_header.nVersion = 1;
                wattx_header.hashPrevBlock.SetNull();
                wattx_header.hashMerkleRoot.SetNull();
                wattx_header.nTime = GetTime();
                wattx_header.nBits = 0x1d00ffff;
                wattx_header.nNonce = 0;

                // Calculate WATTx block hash
                uint256 wattx_hash = wattx_header.GetHash();

                // Create aux chain merkle root (combines block hash with chain ID)
                uint256 aux_merkle_root = auxpow::CalcAuxChainMerkleRoot(
                    wattx_hash, CAuxPowBlockHeader::WATTX_CHAIN_ID);

                // Build merge mining tag
                std::vector<uint8_t> mm_tag = auxpow::BuildMergeMiningTag(aux_merkle_root, 0);

                // Create mock Monero block header
                CMoneroBlockHeader monero_header;
                monero_header.major_version = 16;
                monero_header.minor_version = 0;
                monero_header.timestamp = GetTime();
                monero_header.prev_id.SetNull();
                monero_header.nonce = 12345;
                monero_header.merkle_root.SetNull();

                // Create mock coinbase transaction with merge mining tag
                CMutableTransaction coinbase_tx;
                coinbase_tx.version = 2;

                CTxIn coinbase_in;
                coinbase_in.prevout.SetNull();

                // Put merge mining tag in scriptSig
                std::vector<uint8_t> scriptSig_data;
                scriptSig_data.push_back(0x03);  // Push 3 bytes (height)
                scriptSig_data.push_back(0x01);
                scriptSig_data.push_back(0x00);
                scriptSig_data.push_back(0x00);
                scriptSig_data.insert(scriptSig_data.end(), mm_tag.begin(), mm_tag.end());

                coinbase_in.scriptSig = CScript(scriptSig_data.begin(), scriptSig_data.end());
                coinbase_tx.vin.push_back(coinbase_in);

                CTxOut coinbase_out;
                coinbase_out.nValue = 0;
                coinbase_tx.vout.push_back(coinbase_out);

                // Create AuxPoW proof
                std::vector<uint256> empty_merkle_branch;
                CAuxPow auxpow = auxpow::CreateAuxPow(
                    wattx_header,
                    monero_header,
                    CTransaction(coinbase_tx),
                    empty_merkle_branch,
                    0  // coinbase index
                );

                // Verify the proof extracts the correct merkle root
                uint256 extracted_root;
                bool extracted = auxpow.GetAuxChainMerkleRoot(extracted_root);

                result.pushKV("success", extracted && extracted_root == aux_merkle_root);
                result.pushKV("wattx_block_hash", wattx_hash.GetHex());
                result.pushKV("aux_merkle_root", aux_merkle_root.GetHex());
                result.pushKV("merge_mining_tag", HexStr(mm_tag));
                result.pushKV("tag_size", (int)mm_tag.size());

                if (!extracted) {
                    result.pushKV("error", "Failed to extract aux merkle root from coinbase");
                } else if (extracted_root != aux_merkle_root) {
                    result.pushKV("error", "Extracted merkle root doesn't match expected");
                }

            } catch (const std::exception& e) {
                result.pushKV("success", false);
                result.pushKV("error", std::string("Exception: ") + e.what());
            }

            return result;
        },
    };
}

static RPCHelpMan getmergedstratuminfo()
{
    return RPCHelpMan{"getmergedstratuminfo",
        "\nGet merged mining stratum server status and statistics.\n",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "running", "Whether server is running"},
                {RPCResult::Type::NUM, "clients", "Number of connected clients"},
                {RPCResult::Type::NUM, "total_xmr_shares", "Total Monero shares submitted"},
                {RPCResult::Type::NUM, "total_wtx_shares", "Total WATTx shares submitted"},
                {RPCResult::Type::NUM, "xmr_blocks_found", "Monero blocks found"},
                {RPCResult::Type::NUM, "wtx_blocks_found", "WATTx blocks found"},
            }},
        RPCExamples{
            HelpExampleCli("getmergedstratuminfo", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            auto& server = merged_stratum::GetMergedStratumServer();

            UniValue result(UniValue::VOBJ);
            result.pushKV("running", server.IsRunning());
            result.pushKV("clients", (uint64_t)server.GetClientCount());
            result.pushKV("total_xmr_shares", server.GetTotalXmrShares());
            result.pushKV("total_wtx_shares", server.GetTotalWtxShares());
            result.pushKV("xmr_blocks_found", server.GetXmrBlocksFound());
            result.pushKV("wtx_blocks_found", server.GetWtxBlocksFound());

            return result;
        },
    };
}

void RegisterAnchorRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        // EVM anchor commands
        {"anchor", &getevmanchorinfo},
        {"anchor", &getevmanchor},
        {"anchor", &verifyevmanchor},
        {"anchor", &getevmtxlist},
        {"anchor", &setevmanchoractivation},
        // Private swap commands
        {"swap", &initiateswap},
        {"swap", &getswap},
        {"swap", &joinswap},
        {"swap", &buildswapanchortag},
        {"swap", &getswapstats},
        // Mining rewards commands
        {"mining", &setupminingrewards},
        {"mining", &getminingrewardsstats},
        {"mining", &stopminingrewards},
        // Merged mining test commands
        {"mining", &testauxpowconstruction},
        {"mining", &getmergedstratuminfo},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
