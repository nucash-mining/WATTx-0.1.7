// Copyright (c) 2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_PRIVACY_FCMP_POOL_SCRIPT_H
#define WATTX_PRIVACY_FCMP_POOL_SCRIPT_H

#include <script/script.h>

/**
 * The shielded pool's script and note-output recognition.
 *
 * Separate from fcmp_consensus.h, and built into bitcoin_common rather than
 * wattx_privacy, because transaction POLICY needs these too: a shielded spend is
 * only relayable if the mempool recognises the pool script and a shielded
 * transaction's note outputs. Duplicating the constant in the policy layer would
 * give the pool two definitions that could drift, and the pool's identity is
 * consensus-critical -- every node must agree on it byte for byte.
 */
namespace privacy {

/**
 * @brief The reserved shielded-pool script.
 *
 * A witness program of an as-yet-unassigned version, so pre-activation nodes
 * treat it as anyone-can-spend and this deploys as a softfork. Its security comes
 * from Rule P1 -- spending it requires a valid FCMP payload -- not from script.
 */
const CScript& GetShieldedPoolScript();

/** @brief Is this scriptPubKey the reserved shielded-pool script? */
bool IsPoolScript(const CScript& scriptPubKey);

/**
 * @brief Is this an OP_RETURN publishing an FCMP note?
 *
 * Notes are how shielded outputs enter the curve tree, so a shielded transaction
 * carries one per output -- which is why the "only one OP_RETURN" relay rule has
 * to know about them. This recognises the envelope only (OP_RETURN, push, "FCMP"
 * marker, enough bytes for O||I||C); whether the points are on the curve and
 * whether the note is backed by pool value are consensus questions, answered in
 * fcmp_consensus.cpp.
 */
bool IsFcmpNoteScript(const CScript& scriptPubKey);

} // namespace privacy

#endif // WATTX_PRIVACY_FCMP_POOL_SCRIPT_H
