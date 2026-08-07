// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <privacy/fcmp_tx.h>
#include <array>
#include <privacy/fcmp/fcmp_wrapper.h>
#include <privacy/privacy.h>  // For IsKeyImageSpent
#include <privacy/ed25519/ed25519_types.h>
#include <privacy/ed25519/pedersen.h>
#include <hash.h>
#include <util/strencodings.h>
#include <logging.h>

#include <cstring>

namespace privacy {

// ============================================================================
// Input verification
// ============================================================================

bool VerifyFcmpInput(
    const CFcmpInput& input,
    const curvetree::TreeHash& treeRoot,
    const uint256& messageHash
) {
    // 1. Structurally valid: key image, pseudo-output and proof all present.
    if (!input.IsValid()) {
        return false;
    }

    // 2. The proof must name the root we are verifying against.
    if (input.membershipProof.treeRoot != treeRoot) {
        return false;
    }

    // 3. Verify through fcmp_verify_full -- the audited verifier that checks the
    //    membership proof AND the SA+L signature AND binds the pseudo-output to
    //    the leaf it came from, all against this root and message.
    //
    //    This previously checked a hand-rolled SA+L equation and then called
    //    fcmp_verify, the Schnorr-sigma scaffold. That scaffold proved nothing
    //    about C, so an attacker could present any pseudo-output beside a
    //    "valid" proof and value conservation was unenforceable.
    // Both the key image and the pseudo-output are carried in the 33-byte tagged
    // form (tag byte, then the point); the verifier wants the bare 32-byte point.
    std::array<uint8_t, 32> key_image{};
    std::array<uint8_t, 32> c_tilde{};
    if (input.keyImage.data.size() != 33) return false;
    std::memcpy(key_image.data(), input.keyImage.data.data() + 1, 32);

    // pseudoOutput is the 33-byte tagged commitment; the verifier wants the
    // bare 32-byte point.
    if (input.pseudoOutput.data.size() != 33) return false;
    std::memcpy(c_tilde.data(), input.pseudoOutput.data.data() + 1, 32);

    fcmp::FcmpContext ctx;
    fcmp::FcmpVerifier verifier(treeRoot);

    // Single-layer tree: fcmp_prove_full produces one-layer proofs, and
    // GenerateFullProof refuses to build anything else rather than proving
    // against the wrong root.
    return verifier.VerifyFull(input.membershipProof.proofData,
                               key_image, c_tilde, messageHash, /*num_layers=*/1);
}

bool VerifyFcmpKeyImageUnspent(const CFcmpInput& input) {
    // Check against spent key image database
    return !privacy::IsKeyImageSpent(input.keyImage);
}

bool VerifyFcmpBalance(
    const std::vector<CFcmpInput>& inputs,
    const std::vector<CPedersenCommitment>& outputCommitments,
    CAmount fee
) {
    if (inputs.empty() || outputCommitments.empty()) {
        return false;
    }

    // Sum of pseudo-outputs should equal sum of output commitments + fee*H
    ed25519::Point sumPseudo = ed25519::Point::Identity();

    for (const auto& input : inputs) {
        ed25519::Point pseudoPoint;
        if (!input.pseudoOutput.IsValid()) {
            return false;
        }
        // Convert CPedersenCommitment to ed25519::Point (skip prefix byte)
        if (input.pseudoOutput.data.size() >= 33) {
            std::memcpy(pseudoPoint.data.data(), input.pseudoOutput.data.data() + 1, 32);
        } else if (input.pseudoOutput.data.size() == 32) {
            std::memcpy(pseudoPoint.data.data(), input.pseudoOutput.data.data(), 32);
        } else {
            return false;
        }
        sumPseudo = sumPseudo + pseudoPoint;
    }

    // Sum output commitments
    ed25519::Point sumOutputs = ed25519::Point::Identity();

    for (const auto& commitment : outputCommitments) {
        ed25519::Point outputPoint;
        // Convert CPedersenCommitment to ed25519::Point (skip prefix byte)
        if (commitment.data.size() >= 33) {
            std::memcpy(outputPoint.data.data(), commitment.data.data() + 1, 32);
        } else if (commitment.data.size() == 32) {
            std::memcpy(outputPoint.data.data(), commitment.data.data(), 32);
        } else {
            return false;
        }
        sumOutputs = sumOutputs + outputPoint;
    }

    // Add fee*H to outputs
    auto H = ed25519::PedersenGenerators::Default().H();
    ed25519::Scalar feeScalar(static_cast<uint64_t>(fee));
    auto feeCommitment = feeScalar * H;
    sumOutputs = sumOutputs + feeCommitment;

    // Verify balance: sumPseudo == sumOutputs
    return sumPseudo.data == sumOutputs.data;
}

bool BatchVerifyFcmpInputs(
    const std::vector<CFcmpInput>& inputs,
    const curvetree::TreeHash& treeRoot,
    const uint256& messageHash
) {
    // For now, verify individually
    // Future optimization: use batch verification for signatures and proofs
    for (const auto& input : inputs) {
        if (!VerifyFcmpInput(input, treeRoot, messageHash)) {
            return false;
        }
        if (!VerifyFcmpKeyImageUnspent(input)) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Utility Functions
// ============================================================================

curvetree::OutputTuple OutputToTreeFormat(
    const ed25519::Point& O,
    const ed25519::Point& I,
    const ed25519::Point& C
) {
    curvetree::OutputTuple tuple;
    tuple.O = O;
    tuple.I = I;
    tuple.C = C;
    return tuple;
}

curvetree::OutputTuple GenerateOutputTuple(
    const ed25519::Point& spendPubkey,
    CAmount amount,
    const ed25519::Scalar& blinding
) {
    curvetree::OutputTuple tuple;

    // O = spend public key (one-time address)
    tuple.O = spendPubkey;

    // I = Hp(O) - hash of O to point (key image base)
    std::vector<uint8_t> toHash(spendPubkey.data.begin(), spendPubkey.data.end());
    tuple.I = ed25519::Point::HashToPoint(toHash);

    // C = amount*H + blinding*G (Pedersen commitment)
    auto commitment = ed25519::PedersenCommitment::CommitAmount(
        static_cast<uint64_t>(amount),
        blinding
    );
    tuple.C = commitment.GetPoint();

    return tuple;
}

} // namespace privacy
