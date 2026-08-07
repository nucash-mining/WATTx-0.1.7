// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_PRIVACY_FCMP_TX_H
#define WATTX_PRIVACY_FCMP_TX_H

/**
 * FCMP Transaction Types
 *
 * This file defines transaction structures for Full-Chain Membership Proofs (FCMP++),
 * Monero's next-generation privacy technology that replaces ring signatures.
 *
 * KEY DIFFERENCES FROM RINGCT:
 *
 * RingCT (current):
 *   - Hides sender among small ring of decoys (e.g., 16 members)
 *   - Ring size limits anonymity set
 *   - Decoy selection can leak timing information
 *   - O(ring_size * inputs) signature size
 *
 * FCMP (new):
 *   - Proves membership in ENTIRE output set
 *   - Anonymity set = all outputs ever created
 *   - No decoy selection needed
 *   - O(log(outputs)) proof size using curve trees
 *
 * COMPONENTS:
 *
 * 1. FcmpProof - Zero-knowledge proof of membership
 *    - Proves output exists in curve tree without revealing which
 *    - Uses Bulletproofs for inner product arguments
 *    - Includes commitments for amount balance
 *
 * 2. CFcmpInput - Input using FCMP instead of ring signature
 *    - Key image (for double-spend detection)
 *    - Membership proof
 *    - Pseudo-output commitment
 *
 * 3. CFcmpSignature - Aggregated signature for all inputs
 *    - Proves knowledge of secret keys
 *    - Links key images to membership proofs
 *    - SA+L (Spend Authorization + Linkability)
 */

#include <privacy/ring_signature.h>     // For CKeyImage
#include <privacy/confidential.h>       // For CPedersenCommitment
#include <privacy/ed25519/ed25519_types.h>
#include <privacy/curvetree/curve_tree.h>

#ifdef HAVE_FCMP
#include <privacy/fcmp/fcmp_wrapper.h>
#endif

#include <serialize.h>
#include <uint256.h>

#include <vector>
#include <optional>
#include <memory>

namespace privacy {

// ============================================================================
// FCMP Proof Structures
// ============================================================================

/**
 * @brief Re-randomized input tuple for FCMP verification
 *
 * When spending an output (O, I, C), we create a re-randomized version
 * that hides which specific output is being spent while proving it exists.
 *
 * O_tilde = O + r*G  (re-randomized output point)
 * I_tilde = I        (key image - cannot be re-randomized)
 * C_tilde = C + r*H  (re-randomized commitment)
 *
 * Where r is a random scalar chosen by the spender.
 */
struct CFcmpInputTuple {
    ed25519::Point O_tilde;  // Re-randomized O point
    ed25519::Point I_tilde;  // Key image point (not re-randomized)
    ed25519::Point R;        // R value for SA+L signature
    ed25519::Point C_tilde;  // Re-randomized commitment

    CFcmpInputTuple() = default;

    bool IsValid() const {
        return O_tilde.IsValid() && I_tilde.IsValid() && C_tilde.IsValid();
    }

    bool IsNull() const {
        return !IsValid();
    }

    SERIALIZE_METHODS(CFcmpInputTuple, obj) {
        READWRITE(obj.O_tilde, obj.I_tilde, obj.R, obj.C_tilde);
    }
};

/**
 * @brief FCMP proof data
 *
 * Zero-knowledge proof that an output exists in the curve tree.
 * The proof demonstrates membership without revealing which output.
 */
struct CFcmpProof {
    // Serialized proof bytes (actual proof from Rust library)
    std::vector<uint8_t> proofData;

    // Tree root at time of proof generation (for verification).
    //
    // Curve-TAGGED. An FCMP++ curve tree alternates Selene and Helios, and the
    // same 32 bytes are a valid point on both curves that hash differently, so
    // a root without its curve is ambiguous. This was an ed25519::Point, which
    // could only ever name a root from the old ed25519 -> ed25519 tree -- a
    // hash that is not injective and cannot be opened in the proving circuit.
    //
    // CONSENSUS FORMAT CHANGE. Safe to make now and only now: getfcmpinfo
    // reports tree_size 0, so no shielded output has ever existed on any WATTx
    // chain and there is no proof in existence carrying the old field. The
    // window closes the moment a real shielded user appears.
    curvetree::TreeHash treeRoot;

    // Proof version for future upgrades. Bumped to 2 with the curve-tagged root
    // so a v1 proof is rejected rather than reinterpreted.
    uint8_t version{2};

    CFcmpProof() = default;

    explicit CFcmpProof(std::vector<uint8_t> data, const curvetree::TreeHash& root)
        : proofData(std::move(data)), treeRoot(root) {}

    bool IsValid() const {
        // A zero root is what an empty tree yields; it names nothing.
        static const curvetree::TreeHash empty{};
        return !proofData.empty() && version >= 2 && treeRoot != empty;
    }

    size_t GetSize() const {
        return proofData.size();
    }

    SERIALIZE_METHODS(CFcmpProof, obj) {
        READWRITE(obj.version, obj.proofData);
        uint8_t curve = static_cast<uint8_t>(obj.treeRoot.curve);
        READWRITE(curve);
        SER_READ(obj, obj.treeRoot.curve = static_cast<curvetree::TreeCurve>(curve));
        READWRITE(obj.treeRoot.bytes);
    }
};

/**
 * @brief Spend Authorization + Linkability (SA+L) signature component
 *
 * For each input, we need:
 * - Key image I = x * Hp(O) where x is the secret key
 * - Signature proving knowledge of x
 *
 * The signature uses the Schnorr-like protocol:
 * 1. R = r * G (for some random r)
 * 2. c = H(R || I || O_tilde || message)
 * 3. s = r + c * x
 *
 * Verification: s*G == R + c*(O_tilde - r*G) where r is the re-randomization
 */
struct CFcmpSALSignature {
    ed25519::Scalar c;  // Challenge
    ed25519::Scalar s;  // Response

    CFcmpSALSignature() = default;

    bool IsValid() const {
        return !c.IsZero() || !s.IsZero();  // At least one must be non-zero
    }

    SERIALIZE_METHODS(CFcmpSALSignature, obj) {
        READWRITE(obj.c, obj.s);
    }
};

// ============================================================================
// FCMP Transaction Input
// ============================================================================

/**
 * @brief FCMP-based transaction input
 *
 * Replaces CPrivacyInput for FCMP transactions.
 * Instead of a ring with decoys, we have a membership proof.
 */
struct CFcmpInput {
    // Key image (prevents double-spend)
    // I = x * Hp(O) where x is the secret key for output O
    CKeyImage keyImage;

    // Re-randomized input tuple
    CFcmpInputTuple inputTuple;

    // FCMP membership proof
    CFcmpProof membershipProof;

    // SA+L signature for this input
    CFcmpSALSignature salSignature;

    // Pseudo-output commitment for balance verification
    // The sum of pseudo-outputs must equal sum of real outputs + fee
    CPedersenCommitment pseudoOutput;

    CFcmpInput() = default;

    bool IsValid() const {
        // O~, I~, R and the SA+L signature live INSIDE the membership proof and
        // are read from it by fcmp_verify_full. Requiring hand-supplied copies
        // here would demand values that can drift from what was actually proven;
        // what must be present is the key image, the pseudo-output C~, and the
        // proof itself, which are exactly what verification consumes.
        return keyImage.IsValid() &&
               pseudoOutput.IsValid() &&
               membershipProof.IsValid();
    }

    SERIALIZE_METHODS(CFcmpInput, obj) {
        READWRITE(obj.keyImage, obj.inputTuple, obj.membershipProof,
                  obj.salSignature, obj.pseudoOutput);
    }
};

// ============================================================================
// FCMP Aggregated Signature
// ============================================================================

/**
 * @brief Aggregated signature for FCMP transaction
 *
 * While each input has its own SA+L component, the proofs can be
 * aggregated for efficiency. This structure holds the aggregated
 * proof and linking data.
 */
struct CFcmpAggregatedSig {
    // Aggregated Bulletproof for all membership proofs
    std::vector<uint8_t> aggregatedProof;

    // Combined challenge for all SA+L signatures
    ed25519::Scalar aggregatedChallenge;

    // Version for future upgrades
    uint8_t version{1};

    CFcmpAggregatedSig() = default;

    bool IsValid() const {
        // Can be empty if using individual proofs
        return true;
    }

    SERIALIZE_METHODS(CFcmpAggregatedSig, obj) {
        READWRITE(obj.version, obj.aggregatedProof, obj.aggregatedChallenge);
    }
};

// ============================================================================
// FCMP Transaction Builder
// ============================================================================


// ============================================================================
// FCMP Verification Functions
// ============================================================================

/**
 * @brief Verify an FCMP input
 * @param input The FCMP input to verify
 * @param treeRoot Current curve tree root (or root at block height)
 * @param messageHash Transaction hash for signature verification
 * @return true if valid
 */
bool VerifyFcmpInput(
    const CFcmpInput& input,
    const curvetree::TreeHash& treeRoot,
    const uint256& messageHash
);

/**
 * @brief Verify FCMP input key image is unspent
 * @param input The FCMP input
 * @return true if key image hasn't been spent
 */
bool VerifyFcmpKeyImageUnspent(const CFcmpInput& input);

/**
 * @brief Verify balance of FCMP inputs and outputs
 *
 * Verifies: sum(pseudo_outputs) == sum(output_commitments) + fee*H
 *
 * @param inputs Vector of FCMP inputs
 * @param outputCommitments Output commitments
 * @param fee Transaction fee
 * @return true if balanced
 */
bool VerifyFcmpBalance(
    const std::vector<CFcmpInput>& inputs,
    const std::vector<CPedersenCommitment>& outputCommitments,
    CAmount fee
);

/**
 * @brief Batch verify multiple FCMP inputs
 *
 * More efficient than verifying individually.
 *
 * @param inputs Vector of FCMP inputs to verify
 * @param treeRoot Current curve tree root
 * @param messageHash Transaction hash
 * @return true if all valid
 */
bool BatchVerifyFcmpInputs(
    const std::vector<CFcmpInput>& inputs,
    const curvetree::TreeHash& treeRoot,
    const uint256& messageHash
);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Convert output tuple to curve tree format
 */
curvetree::OutputTuple OutputToTreeFormat(
    const ed25519::Point& O,
    const ed25519::Point& I,
    const ed25519::Point& C
);

/**
 * @brief Generate output points from spending keys
 *
 * O = spend_pubkey (one-time address)
 * I = key_image_base = Hp(O)
 * C = commitment = amount*H + blinding*G
 */
curvetree::OutputTuple GenerateOutputTuple(
    const ed25519::Point& spendPubkey,
    CAmount amount,
    const ed25519::Scalar& blinding
);

} // namespace privacy

#endif // WATTX_PRIVACY_FCMP_TX_H
