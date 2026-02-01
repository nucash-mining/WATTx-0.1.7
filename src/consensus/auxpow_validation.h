// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_CONSENSUS_AUXPOW_VALIDATION_H
#define WATTX_CONSENSUS_AUXPOW_VALIDATION_H

#include <auxpow/auxpow.h>
#include <consensus/params.h>
#include <primitives/block.h>

namespace consensus {

/**
 * Check if AuxPoW is active at the given block height
 * @param height Block height to check
 * @param params Consensus parameters
 * @return true if AuxPoW (merged mining) is allowed at this height
 */
bool IsAuxPowActive(int height, const Consensus::Params& params);

/**
 * Validate a block header that may be either standard PoW or AuxPoW
 *
 * This function handles both merged-mined blocks (with parent chain proof)
 * and standalone RandomX blocks.
 *
 * @param header Block header to validate
 * @param nBits Difficulty target
 * @param params Consensus parameters
 * @return true if the proof of work is valid
 */
bool CheckBlockProofOfWork(const CBlockHeader& header, unsigned int nBits,
                            const Consensus::Params& params);

/**
 * Validate an AuxPoW proof specifically
 *
 * Verifies:
 * 1. Parent block (Monero) meets WATTx difficulty target
 * 2. Coinbase contains correct WATTx block hash commitment
 * 3. Merkle proofs are valid
 *
 * @param header WATTx block header
 * @param auxpow The auxiliary proof of work
 * @param nBits WATTx difficulty target
 * @param params Consensus parameters
 * @return true if the AuxPoW proof is valid
 */
bool CheckAuxProofOfWork(const CBlockHeader& header, const CAuxPow& auxpow,
                          unsigned int nBits, const Consensus::Params& params);

/**
 * Get the proof-of-work hash for a block
 *
 * For AuxPoW blocks, this returns the parent block's RandomX hash.
 * For standard blocks, this returns the block's own RandomX hash.
 *
 * @param header Block header
 * @param auxpow Optional AuxPoW data (null for standard blocks)
 * @return The PoW hash to compare against target
 */
uint256 GetBlockPoWHash(const CBlockHeader& header,
                         const CAuxPow* auxpow = nullptr);

/**
 * Extended consensus parameters for AuxPoW
 */
struct AuxPowParams {
    // Height at which AuxPoW becomes active
    int nAuxPowActivationHeight{0};

    // Minimum height difference between parent chain blocks
    // (prevents submitting the same parent block for multiple aux blocks)
    int nMinParentBlockDelta{1};

    // Maximum age of parent block timestamp vs aux block timestamp
    int nMaxParentTimeDiff{7200};  // 2 hours

    // WATTx chain ID (must match in AuxPoW proof)
    int nChainId{CAuxPowBlockHeader::WATTX_CHAIN_ID};

    // Whether to allow standalone (non-merged) mining after activation
    bool fAllowStandaloneMining{true};
};

/**
 * Get AuxPoW parameters for the current chain
 */
const AuxPowParams& GetAuxPowParams();

/**
 * Set AuxPoW parameters (for testing/configuration)
 */
void SetAuxPowParams(const AuxPowParams& params);

}  // namespace consensus

#endif  // WATTX_CONSENSUS_AUXPOW_VALIDATION_H
