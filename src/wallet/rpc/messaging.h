// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_RPC_MESSAGING_H
#define BITCOIN_WALLET_RPC_MESSAGING_H

#include <span.h>

class CRPCCommand;

namespace wallet {
Span<const CRPCCommand> GetMessagingRPCCommands();
} // namespace wallet

#endif // BITCOIN_WALLET_RPC_MESSAGING_H
