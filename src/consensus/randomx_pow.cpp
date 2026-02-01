// Copyright (c) 2024 The WATTx developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/params.h>
#include <primitives/block.h>
#include <uint256.h>
#include <node/randomx_miner.h>
#include <arith_uint256.h>
#include <logging.h>
#include <crypto/sha256.h>

/**
 * RandomX Proof-of-Work consensus validation
 *
 * This file provides consensus-level validation for RandomX PoW blocks.
 * The actual RandomX hashing is performed by the RandomXMiner class.
 */

namespace Consensus {

/**
 * Validate a RandomX proof-of-work
 *
 * @param header The block header to validate
 * @param params Consensus parameters
 * @return true if the PoW is valid
 */
bool CheckRandomXProofOfWork(const CBlockHeader& header, const Params& params)
{
    // Get the RandomX hash
    auto headerData = node::RandomXMiner::SerializeBlockHeader(header);

    uint256 hash;
    node::GetRandomXMiner().CalculateHash(headerData.data(), headerData.size(), hash.data());

    if (hash.IsNull()) {
        LogPrintf("CheckRandomXProofOfWork: Failed to compute RandomX hash\n");
        return false;
    }

    // Check against target
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;
    bnTarget.SetCompact(header.nBits, &fNegative, &fOverflow);

    if (fNegative || bnTarget == 0 || fOverflow) {
        return false;
    }

    if (bnTarget > UintToArith256(params.powLimit)) {
        return false;
    }

    if (UintToArith256(hash) > bnTarget) {
        return false;
    }

    return true;
}

/**
 * Get the RandomX key for a given block height
 * The key changes every 2048 blocks for security
 */
uint256 GetRandomXKey(int nHeight, const uint256& genesisHash)
{
    // Key epoch: changes every 2048 blocks
    int keyEpoch = nHeight / 2048;

    // Create key by hashing epoch + genesis
    uint256 key;
    CSHA256 hasher;
    hasher.Write(reinterpret_cast<const unsigned char*>(&keyEpoch), sizeof(keyEpoch));
    hasher.Write(genesisHash.begin(), 32);
    hasher.Finalize(key.begin());

    return key;
}

} // namespace Consensus
