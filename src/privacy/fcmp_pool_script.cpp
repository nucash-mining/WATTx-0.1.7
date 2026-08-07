// Copyright (c) 2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <privacy/fcmp_pool_script.h>

#include <hash.h>
#include <uint256.h>

#include <span>
#include <string>
#include <vector>

namespace privacy {

const CScript& GetShieldedPoolScript()
{
    // Witness program, version 16 (as-yet unassigned), 32-byte domain-separated
    // program. Unassigned witness versions are anyone-can-spend to nodes that do
    // not know the rule, which is what lets this deploy as a softfork; Rule P1 is
    // what actually protects the funds.
    //
    // The program is a fixed constant, NOT a hash of anything transaction-specific:
    // there is exactly one pool, and every node must recognise it byte-for-byte
    // without needing any context to derive it.
    static const CScript pool_script = [] {
        const std::string tag = "WATTx FCMP shielded pool v1";
        const uint256 program = Hash(std::span<const unsigned char>(
            reinterpret_cast<const unsigned char*>(tag.data()), tag.size()));
        CScript s;
        s << OP_16 << std::vector<unsigned char>(program.begin(), program.end());
        return s;
    }();
    return pool_script;
}

bool IsPoolScript(const CScript& scriptPubKey)
{
    return scriptPubKey == GetShieldedPoolScript();
}

bool IsFcmpNoteScript(const CScript& scriptPubKey)
{
    // OP_RETURN, a push, then "FCMP" and at least O||I||C. Must stay in step with
    // ExtractFcmpOutputs, which is what actually reads the note.
    if (scriptPubKey.size() < 103 || scriptPubKey[0] != OP_RETURN) {
        return false;
    }

    size_t dataOffset = 0;
    if (scriptPubKey[1] == 0x4c) {        // OP_PUSHDATA1
        dataOffset = 3;
    } else if (scriptPubKey[1] < 0x4c) {  // direct push
        dataOffset = 2;
    } else {
        return false;
    }

    if (scriptPubKey.size() < dataOffset + 100) return false;

    return scriptPubKey[dataOffset] == 0x46 &&      // 'F'
           scriptPubKey[dataOffset + 1] == 0x43 &&  // 'C'
           scriptPubKey[dataOffset + 2] == 0x4D &&  // 'M'
           scriptPubKey[dataOffset + 3] == 0x50;    // 'P'
}

} // namespace privacy
