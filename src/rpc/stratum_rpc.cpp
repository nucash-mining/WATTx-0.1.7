// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <stratum/stratum_server.h>
#include <stratum/merged_stratum.h>
#include <stratum/multi_merged_stratum.h>
#include <stratum/parent_chain.h>
#include <interfaces/mining.h>
#include <node/context.h>
#include <univalue.h>

using node::NodeContext;

static RPCHelpMan startstratum()
{
    return RPCHelpMan{"startstratum",
        "\nStart the stratum mining server for XMRig.\n",
        {
            {"port", RPCArg::Type::NUM, RPCArg::Default{3335}, "Port to listen on"},
            {"address", RPCArg::Type::STR, RPCArg::Default{"0.0.0.0"}, "Address to bind to"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "success", "Whether server started successfully"},
                {RPCResult::Type::NUM, "port", "Port the server is listening on"},
            }
        },
        RPCExamples{
            HelpExampleCli("startstratum", "")
            + HelpExampleCli("startstratum", "3335")
            + HelpExampleCli("startstratum", "3335 \"127.0.0.1\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            NodeContext& node = EnsureAnyNodeContext(request.context);

            stratum::StratumConfig config;
            config.port = request.params[0].isNull() ? 3335 : request.params[0].getInt<int>();
            config.bind_address = request.params[1].isNull() ? "0.0.0.0" : request.params[1].get_str();

            stratum::StratumServer& server = stratum::GetStratumServer();

            if (server.IsRunning()) {
                throw JSONRPCError(RPC_MISC_ERROR, "Stratum server already running");
            }

            bool success = server.Start(config, node.mining.get());

            UniValue result(UniValue::VOBJ);
            result.pushKV("success", success);
            result.pushKV("port", (int)server.GetPort());
            return result;
        },
    };
}

static RPCHelpMan stopstratum()
{
    return RPCHelpMan{"stopstratum",
        "\nStop the stratum mining server.\n",
        {},
        RPCResult{
            RPCResult::Type::BOOL, "", "Always returns true"
        },
        RPCExamples{
            HelpExampleCli("stopstratum", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            stratum::StratumServer& server = stratum::GetStratumServer();
            server.Stop();
            return true;
        },
    };
}

static RPCHelpMan getstratuminfo()
{
    return RPCHelpMan{"getstratuminfo",
        "\nGet information about the stratum server.\n",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "running", "Whether the server is running"},
                {RPCResult::Type::NUM, "port", "Port the server is listening on"},
                {RPCResult::Type::NUM, "clients", "Number of connected miners"},
                {RPCResult::Type::NUM, "shares_accepted", "Total accepted shares"},
                {RPCResult::Type::NUM, "shares_rejected", "Total rejected shares"},
                {RPCResult::Type::NUM, "blocks_found", "Total blocks found"},
            }
        },
        RPCExamples{
            HelpExampleCli("getstratuminfo", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            stratum::StratumServer& server = stratum::GetStratumServer();

            UniValue result(UniValue::VOBJ);
            result.pushKV("running", server.IsRunning());
            result.pushKV("port", (int)server.GetPort());
            result.pushKV("clients", (int)server.GetClientCount());
            result.pushKV("shares_accepted", (uint64_t)server.GetTotalSharesAccepted());
            result.pushKV("shares_rejected", (uint64_t)server.GetTotalSharesRejected());
            result.pushKV("blocks_found", (uint64_t)server.GetBlocksFound());
            return result;
        },
    };
}

static RPCHelpMan startmergedstratum()
{
    return RPCHelpMan{"startmergedstratum",
        "\nStart the merged mining stratum server for mining WATTx via parent chains (e.g., Monero).\n",
        {
            {"port", RPCArg::Type::NUM, RPCArg::Default{3337}, "Port to listen on"},
            {"monero_host", RPCArg::Type::STR, RPCArg::Default{"127.0.0.1"}, "Monero daemon host"},
            {"monero_port", RPCArg::Type::NUM, RPCArg::Default{18081}, "Monero daemon port"},
            {"monero_wallet", RPCArg::Type::STR, RPCArg::Optional::NO, "Monero wallet address for block rewards"},
            {"wattx_wallet", RPCArg::Type::STR, RPCArg::Optional::NO, "WATTx wallet address for block rewards"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "success", "Whether server started successfully"},
                {RPCResult::Type::NUM, "port", "Port the server is listening on"},
            }
        },
        RPCExamples{
            HelpExampleCli("startmergedstratum", "3337 \"127.0.0.1\" 18081 \"4...MoneroAddr\" \"W...WATTxAddr\"")
            + HelpExampleRpc("startmergedstratum", "3337, \"127.0.0.1\", 18081, \"4...MoneroAddr\", \"W...WATTxAddr\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            NodeContext& node = EnsureAnyNodeContext(request.context);

            merged_stratum::MergedStratumConfig config;
            config.port = request.params[0].isNull() ? 3337 : request.params[0].getInt<int>();
            config.monero_daemon_host = request.params[1].isNull() ? "127.0.0.1" : request.params[1].get_str();
            config.monero_daemon_port = request.params[2].isNull() ? 18081 : request.params[2].getInt<int>();
            config.monero_wallet_address = request.params[3].get_str();
            config.wattx_wallet_address = request.params[4].get_str();

            merged_stratum::MergedStratumServer& server = merged_stratum::GetMergedStratumServer();

            if (server.IsRunning()) {
                throw JSONRPCError(RPC_MISC_ERROR, "Merged stratum server already running");
            }

            bool success = server.Start(config, node.mining.get());

            UniValue result(UniValue::VOBJ);
            result.pushKV("success", success);
            result.pushKV("port", config.port);
            return result;
        },
    };
}

static RPCHelpMan stopmergedstratum()
{
    return RPCHelpMan{"stopmergedstratum",
        "\nStop the merged mining stratum server.\n",
        {},
        RPCResult{
            RPCResult::Type::BOOL, "", "Always returns true"
        },
        RPCExamples{
            HelpExampleCli("stopmergedstratum", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            merged_stratum::MergedStratumServer& server = merged_stratum::GetMergedStratumServer();
            server.Stop();
            return true;
        },
    };
}

static RPCHelpMan startbitcoinmergedstratum()
{
    return RPCHelpMan{"startbitcoinmergedstratum",
        "\nStart merged mining stratum server for Bitcoin/SHA256d parent chain.\n",
        {
            {"port", RPCArg::Type::NUM, RPCArg::Default{3338}, "Port to listen on"},
            {"bitcoin_host", RPCArg::Type::STR, RPCArg::Default{"127.0.0.1"}, "Bitcoin daemon host"},
            {"bitcoin_port", RPCArg::Type::NUM, RPCArg::Default{8332}, "Bitcoin RPC port"},
            {"bitcoin_user", RPCArg::Type::STR, RPCArg::Optional::NO, "Bitcoin RPC username"},
            {"bitcoin_pass", RPCArg::Type::STR, RPCArg::Optional::NO, "Bitcoin RPC password"},
            {"wattx_wallet", RPCArg::Type::STR, RPCArg::Optional::NO, "WATTx wallet address for block rewards"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "success", "Whether server started successfully"},
                {RPCResult::Type::NUM, "port", "Port the server is listening on"},
                {RPCResult::Type::STR, "chain", "Parent chain type"},
            }
        },
        RPCExamples{
            HelpExampleCli("startbitcoinmergedstratum", "3338 \"127.0.0.1\" 18332 \"btcuser\" \"btcpass\" \"WATTxAddr\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            NodeContext& node = EnsureAnyNodeContext(request.context);

            // Configure Bitcoin parent chain
            merged_stratum::ParentChainConfig btc_config;
            btc_config.name = "bitcoin";
            btc_config.chain_id = 1;
            btc_config.algo = merged_stratum::ParentChainAlgo::SHA256D;
            btc_config.daemon_host = request.params[1].isNull() ? "127.0.0.1" : request.params[1].get_str();
            btc_config.daemon_port = request.params[2].isNull() ? 8332 : request.params[2].getInt<int>();
            btc_config.daemon_user = request.params[3].get_str();
            btc_config.daemon_password = request.params[4].get_str();
            btc_config.wallet_address = "";  // Bitcoin doesn't need wallet for getblocktemplate

            // Configure multi-chain server
            merged_stratum::MultiMergedConfig config;
            config.base_port = request.params[0].isNull() ? 3338 : request.params[0].getInt<int>();
            config.wattx_wallet_address = request.params[5].get_str();
            config.parent_chains.push_back(btc_config);

            merged_stratum::MultiMergedStratumServer& server = merged_stratum::GetMultiMergedStratumServer();

            if (server.IsRunning()) {
                throw JSONRPCError(RPC_MISC_ERROR, "Multi-merged stratum server already running");
            }

            bool success = server.Start(config, node.mining.get());

            UniValue result(UniValue::VOBJ);
            result.pushKV("success", success);
            result.pushKV("port", (int)config.base_port);
            result.pushKV("chain", "bitcoin");
            return result;
        },
    };
}

void RegisterStratumRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"mining", &startstratum},
        {"mining", &stopstratum},
        {"mining", &getstratuminfo},
        {"mining", &startbitcoinmergedstratum},
        {"mining", &startmergedstratum},
        {"mining", &stopmergedstratum},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
