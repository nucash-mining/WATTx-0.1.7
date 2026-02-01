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

    // Tree root at time of proof generation (for verification)
    ed25519::Point treeRoot;

    // Proof version for future upgrades
    uint8_t version{1};

    CFcmpProof() = default;

    explicit CFcmpProof(std::vector<uint8_t> data, const ed25519::Point& root)
        : proofData(std::move(data)), treeRoot(root) {}

    bool IsValid() const {
        return !proofData.empty() && treeRoot.IsValid();
    }

    size_t GetSize() const {
        return proofData.size();
    }

    SERIALIZE_METHODS(CFcmpProof, obj) {
        READWRITE(obj.version, obj.proofData, obj.treeRoot);
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
        return keyImage.IsValid() &&
               inputTuple.IsValid() &&
               membershipProof.IsValid() &&
               salSignature.IsValid();
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

/**
 * @brief Builder for creating FCMP transactions
 *
 * Similar to CPrivacyTransactionBuilder but uses FCMP proofs
 * instead of ring signatures.
 */
class CFcmpTransactionBuilder {
public:
    /**
     * @brief Create a builder with the given curve tree
     * @param tree Shared pointer to the curve tree for membership proofs
     */
    explicit CFcmpTransactionBuilder(std::shared_ptr<curvetree::CurveTree> tree);

    /**
     * @brief Add an input to spend
     * @param leafIndex Index of the output in the curve tree
     * @param output The output tuple (O, I, C)
     * @param secretKey The secret key for spending
     * @param amount The input amount (for balance)
     * @param blindingFactor The blinding factor for the commitment
     * @return true if input added successfully
     */
    bool AddInput(
        uint64_t leafIndex,
        const curvetree::OutputTuple& output,
        const ed25519::Scalar& secretKey,
        CAmount amount,
        const ed25519::Scalar& blindingFactor
    );

    /**
     * @brief Add an output
     * @param output The output tuple to create
     * @param amount The output amount
     * @param blindingFactor The blinding factor for the commitment
     * @return true if output added successfully
     */
    bool AddOutput(
        const curvetree::OutputTuple& output,
        CAmount amount,
        const ed25519::Scalar& blindingFactor
    );

    /**
     * @brief Set the transaction fee
     */
    void SetFee(CAmount fee);

    /**
     * @brief Build the FCMP inputs
     * @return Vector of CFcmpInput, or empty on failure
     */
    std::vector<CFcmpInput> BuildInputs();

    /**
     * @brief Get the sum of input amounts
     */
    CAmount GetInputSum() const;

    /**
     * @brief Get the sum of output amounts
     */
    CAmount GetOutputSum() const;

    /**
     * @brief Verify the balance (inputs == outputs + fee)
     */
    bool VerifyBalance() const;

private:
    std::shared_ptr<curvetree::CurveTree> m_tree;

    struct InputData {
        uint64_t leafIndex;
        curvetree::OutputTuple output;
        ed25519::Scalar secretKey;
        CAmount amount;
        ed25519::Scalar blindingFactor;
    };

    struct OutputData {
        curvetree::OutputTuple output;
        CAmount amount;
        ed25519::Scalar blindingFactor;
    };

    std::vector<InputData> m_inputs;
    std::vector<OutputData> m_outputs;
    CAmount m_fee{0};

    // Generate a re-randomized input tuple
    CFcmpInputTuple ReRandomizeInput(
        const curvetree::OutputTuple& output,
        ed25519::Scalar& rerandomizer
    );

    // Generate key image from secret key and output
    CKeyImage GenerateKeyImage(
        const ed25519::Scalar& secretKey,
        const ed25519::Point& outputPoint
    );

    // Generate SA+L signature
    CFcmpSALSignature GenerateSALSignature(
        const ed25519::Scalar& secretKey,
        const ed25519::Scalar& rerandomizer,
        const CFcmpInputTuple& inputTuple,
        const uint256& messageHash
    );
};

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
    const ed25519::Point& treeRoot,
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
    const ed25519::Point& treeRoot,
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
