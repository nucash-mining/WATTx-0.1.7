// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/auxpow_validation.h>
#include <arith_uint256.h>
#include <auxpow/auxpow.h>
#include <hash.h>
#include <logging.h>
#include <node/randomx_miner.h>
#include <util/time.h>

namespace consensus {

// Global AuxPoW parameters
static AuxPowParams g_auxpow_params;

const AuxPowParams& GetAuxPowParams() {
    return g_auxpow_params;
}

void SetAuxPowParams(const AuxPowParams& params) {
    g_auxpow_params = params;
}

bool IsAuxPowActive(int height, const Consensus::Params& params) {
    // AuxPoW activates at the configured height
    // Before activation, only standard RandomX mining is allowed
    return height >= g_auxpow_params.nAuxPowActivationHeight;
}

uint256 GetBlockPoWHash(const CBlockHeader& header, const CAuxPow* auxpow) {
    if (auxpow != nullptr) {
        // Merged-mined block: use parent block's RandomX hash
        return auxpow->GetParentBlockPoWHash();
    }

    // Standard block: use our own RandomX hash
    auto blob = node::RandomXMiner::SerializeBlockHeader(header);
    uint256 hash;
    auto& miner = node::GetRandomXMiner();
    if (miner.IsInitialized()) {
        miner.CalculateHash(blob.data(), blob.size(), hash.data());
    } else {
        // Fallback for when RandomX is not initialized
        hash = Hash(blob);
    }
    return hash;
}

bool CheckAuxProofOfWork(const CBlockHeader& header, const CAuxPow& auxpow,
                          unsigned int nBits, const Consensus::Params& params) {
    // 1. Verify chain ID matches
    if (auxpow.nChainId != g_auxpow_params.nChainId) {
        LogPrintf("AuxPoW Validation: Chain ID mismatch (got %d, expected %d)\n",
                  auxpow.nChainId, g_auxpow_params.nChainId);
        return false;
    }

    // 2. Get the parent block's PoW hash (RandomX hash of Monero block)
    uint256 parentPoWHash = auxpow.GetParentBlockPoWHash();

    // 3. Check if parent block hash meets WATTx difficulty target
    arith_uint256 target;
    bool fNegative, fOverflow;
    target.SetCompact(nBits, &fNegative, &fOverflow);

    if (fNegative || fOverflow || target == 0) {
        LogPrintf("AuxPoW Validation: Invalid nBits target\n");
        return false;
    }

    arith_uint256 hashArith = UintToArith256(parentPoWHash);
    if (hashArith > target) {
        LogPrintf("AuxPoW Validation: Parent block hash doesn't meet WATTx target\n");
        LogPrintf("  Hash:   %s\n", parentPoWHash.GetHex());
        LogPrintf("  Target: %s\n", ArithToUint256(target).GetHex());
        return false;
    }

    // 4. Verify the AuxPoW proof structure
    uint256 hashAuxBlock = header.GetHash();
    if (!auxpow.Check(hashAuxBlock, g_auxpow_params.nChainId)) {
        LogPrintf("AuxPoW Validation: Proof structure invalid\n");
        return false;
    }

    // 5. Validate parent block timestamp
    int64_t parentTime = auxpow.parentBlock.timestamp;
    int64_t auxTime = header.nTime;
    int64_t timeDiff = std::abs((int64_t)(parentTime - auxTime));

    if (timeDiff > g_auxpow_params.nMaxParentTimeDiff) {
        LogPrintf("AuxPoW Validation: Parent block timestamp too far from aux block\n");
        LogPrintf("  Parent time: %ld, Aux time: %ld, Diff: %ld (max: %d)\n",
                  parentTime, auxTime, timeDiff, g_auxpow_params.nMaxParentTimeDiff);
        return false;
    }

    LogPrintf("AuxPoW Validation: Proof valid for block %s\n",
              hashAuxBlock.GetHex().substr(0, 16));
    return true;
}

bool CheckBlockProofOfWork(const CBlockHeader& header, unsigned int nBits,
                            const Consensus::Params& params) {
    // Check version flag to determine if this is an AuxPoW block
    bool isAuxPow = (header.nVersion & CAuxPowBlockHeader::AUXPOW_VERSION_FLAG) != 0;

    // If AuxPoW flag is set but we don't have the proof, this is invalid
    // Note: The actual AuxPoW data would be provided separately during full validation
    // This function handles the header-only case

    if (isAuxPow) {
        // For header-only validation, we can't fully verify AuxPoW
        // This will be checked again during full block validation
        // For now, just check basic header validity

        // Check that nBits target is valid
        arith_uint256 target;
        bool fNegative, fOverflow;
        target.SetCompact(nBits, &fNegative, &fOverflow);

        if (fNegative || fOverflow || target == 0) {
            LogPrintf("AuxPoW Block: Invalid nBits target\n");
            return false;
        }

        // Header-only check passes, full validation happens with block body
        return true;
    }

    // Standard PoW block - use RandomX validation
    uint256 hash = GetBlockPoWHash(header, nullptr);

    arith_uint256 target;
    bool fNegative, fOverflow;
    target.SetCompact(nBits, &fNegative, &fOverflow);

    if (fNegative || fOverflow || target == 0) {
        LogPrintf("Standard Block: Invalid nBits target\n");
        return false;
    }

    arith_uint256 hashArith = UintToArith256(hash);
    if (hashArith > target) {
        // Only log at debug level for header sync
        LogPrintf("Standard Block: Hash doesn't meet target\n");
        return false;
    }

    return true;
}

}  // namespace consensus
