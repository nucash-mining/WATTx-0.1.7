// Copyright (c) 2024 The WATTx developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POS_UTXO_TRACKER_H
#define BITCOIN_POS_UTXO_TRACKER_H

#include <primitives/transaction.h>
#include <sync.h>
#include <uint256.h>

#include <map>
#include <set>

/**
 * CoinstakeUTXOTracker - Consensus-level tracking of UTXOs used in coinstake transactions.
 *
 * This prevents the double-spending vulnerability where the same UTXO could be used
 * in multiple coinstake transactions on different chain branches simultaneously.
 *
 * The tracker maintains a set of recently used coinstake prevouts (UTXOs) and
 * the block heights at which they were used. When a coinstake is validated,
 * we check if its prevout is already tracked. If so, the coinstake is rejected
 * unless the block height indicates a reorg situation.
 */
class CoinstakeUTXOTracker
{
private:
    mutable RecursiveMutex cs_tracker;

    // Map from UTXO (prevout) to the block height where it was used in a coinstake
    std::map<COutPoint, int> m_used_coinstake_utxos GUARDED_BY(cs_tracker);

    // Maximum number of blocks to track (after this, UTXOs are considered "old" and can be reused)
    // This should be greater than the maximum expected reorg depth
    static constexpr int MAX_TRACKING_DEPTH = 500;

public:
    CoinstakeUTXOTracker() = default;

    /**
     * Check if a UTXO is available for use in a coinstake at the given height.
     *
     * @param prevout The UTXO to check
     * @param nHeight The current block height being validated
     * @return true if the UTXO can be used, false if it's already used in a recent coinstake
     */
    bool IsUTXOAvailableForStaking(const COutPoint& prevout, int nHeight) const
    {
        LOCK(cs_tracker);

        auto it = m_used_coinstake_utxos.find(prevout);
        if (it == m_used_coinstake_utxos.end()) {
            return true; // Not tracked, available for use
        }

        // UTXO was used before - check if it's from a block that could be reorged
        // If the previous use was at or after our current height, this is a potential
        // double-spend attempt (same UTXO used in competing blocks)
        int usedAtHeight = it->second;

        // Allow if the previous use was deep enough (past reorg possibility)
        // or if we're at a lower height (indicating a reorg is happening)
        if (nHeight < usedAtHeight) {
            return true; // Reorg scenario - allow
        }

        // Don't allow if used at same height or recent height (competing coinstakes)
        if (nHeight - usedAtHeight < 6) { // Within 6 blocks is considered "competing"
            return false;
        }

        return true;
    }

    /**
     * Mark a UTXO as used in a coinstake at the given height.
     * Called when a block containing a coinstake is connected.
     *
     * @param prevout The UTXO used in the coinstake
     * @param nHeight The block height
     */
    void MarkUTXOUsed(const COutPoint& prevout, int nHeight)
    {
        LOCK(cs_tracker);
        m_used_coinstake_utxos[prevout] = nHeight;

        // Prune old entries to prevent unbounded growth
        PruneOldEntries(nHeight);
    }

    /**
     * Unmark a UTXO when a block is disconnected (reorg).
     *
     * @param prevout The UTXO to unmark
     * @param nHeight The block height being disconnected
     */
    void UnmarkUTXO(const COutPoint& prevout, int nHeight)
    {
        LOCK(cs_tracker);

        auto it = m_used_coinstake_utxos.find(prevout);
        if (it != m_used_coinstake_utxos.end() && it->second == nHeight) {
            m_used_coinstake_utxos.erase(it);
        }
    }

    /**
     * Clear all tracking data. Used during initialization or testing.
     */
    void Clear()
    {
        LOCK(cs_tracker);
        m_used_coinstake_utxos.clear();
    }

    /**
     * Get the number of tracked UTXOs. For debugging/monitoring.
     */
    size_t Size() const
    {
        LOCK(cs_tracker);
        return m_used_coinstake_utxos.size();
    }

private:
    /**
     * Remove entries older than MAX_TRACKING_DEPTH blocks.
     */
    void PruneOldEntries(int currentHeight) EXCLUSIVE_LOCKS_REQUIRED(cs_tracker)
    {
        int cutoffHeight = currentHeight - MAX_TRACKING_DEPTH;
        if (cutoffHeight <= 0) return;

        for (auto it = m_used_coinstake_utxos.begin(); it != m_used_coinstake_utxos.end(); ) {
            if (it->second < cutoffHeight) {
                it = m_used_coinstake_utxos.erase(it);
            } else {
                ++it;
            }
        }
    }
};

/**
 * Global singleton for coinstake UTXO tracking.
 * This must be accessed through GetCoinstakeTracker() to ensure proper initialization.
 */
CoinstakeUTXOTracker& GetCoinstakeTracker();

#endif // BITCOIN_POS_UTXO_TRACKER_H
