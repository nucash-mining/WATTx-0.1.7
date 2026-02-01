// Copyright (c) 2024 The WATTx developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <node/randomx_miner.h>
#include <univalue.h>
#include <chainparams.h>

/**
 * RandomX Mining RPC Commands
 *
 * Provides RPC interface for RandomX-specific mining operations.
 */

static RPCHelpMan getrandomxinfo()
{
    return RPCHelpMan{"getrandomxinfo",
        "\nReturns RandomX mining information.\n",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "initialized", "Whether RandomX is initialized"},
                {RPCResult::Type::BOOL, "mining", "Whether mining is active"},
                {RPCResult::Type::NUM, "hashrate", "Current hashrate (H/s)"},
                {RPCResult::Type::NUM, "total_hashes", "Total hashes computed"},
                {RPCResult::Type::BOOL, "hardware_aes", "Whether hardware AES is available"},
                {RPCResult::Type::BOOL, "large_pages", "Whether large pages are available"},
            }
        },
        RPCExamples{
            HelpExampleCli("getrandomxinfo", "")
            + HelpExampleRpc("getrandomxinfo", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    auto& miner = node::GetRandomXMiner();

    UniValue result(UniValue::VOBJ);
    result.pushKV("initialized", miner.IsInitialized());
    result.pushKV("mining", miner.IsMining());
    result.pushKV("hashrate", miner.GetHashrate());
    result.pushKV("total_hashes", (uint64_t)miner.GetTotalHashes());
    result.pushKV("hardware_aes", node::RandomXMiner::HasHardwareAES());
    result.pushKV("large_pages", node::RandomXMiner::HasLargePages());

    return result;
},
    };
}

static RPCHelpMan initrandomx()
{
    return RPCHelpMan{"initrandomx",
        "\nInitialize RandomX for mining.\n",
        {
            {"mode", RPCArg::Type::STR, RPCArg::Default{"light"}, "Mode: 'light' (~256MB) or 'full' (~2GB, faster)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "success", "Whether initialization succeeded"},
                {RPCResult::Type::STR, "mode", "Initialized mode"},
                {RPCResult::Type::STR, "message", "Status message"},
            }
        },
        RPCExamples{
            HelpExampleCli("initrandomx", "\"full\"")
            + HelpExampleRpc("initrandomx", "\"full\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::string modeStr = "light";
    if (!request.params[0].isNull()) {
        modeStr = request.params[0].get_str();
    }

    node::RandomXMiner::Mode mode = node::RandomXMiner::Mode::LIGHT;
    if (modeStr == "full") {
        mode = node::RandomXMiner::Mode::FULL;
    }

    auto& miner = node::GetRandomXMiner();
    const auto& params = Params().GetConsensus();

    bool success = miner.Initialize(
        params.hashGenesisBlock.data(),
        32,
        mode
    );

    UniValue result(UniValue::VOBJ);
    result.pushKV("success", success);
    result.pushKV("mode", mode == node::RandomXMiner::Mode::FULL ? "FULL" : "LIGHT");
    result.pushKV("message", success ? "RandomX initialized successfully" : "RandomX initialization failed");

    return result;
},
    };
}

void RegisterRandomXMiningRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"mining", &getrandomxinfo},
        {"mining", &initrandomx},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
