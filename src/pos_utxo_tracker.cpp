// Copyright (c) 2024 The WATTx developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pos_utxo_tracker.h>

/**
 * Global singleton instance of the coinstake UTXO tracker.
 * This tracks UTXOs used in coinstake transactions to prevent double-spending.
 */
CoinstakeUTXOTracker& GetCoinstakeTracker()
{
    static CoinstakeUTXOTracker instance;
    return instance;
}
