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
// CFcmpTransactionBuilder Implementation
// ============================================================================

CFcmpTransactionBuilder::CFcmpTransactionBuilder(
    std::shared_ptr<curvetree::CurveTree> tree
) : m_tree(std::move(tree)) {}

bool CFcmpTransactionBuilder::AddInput(
    uint64_t leafIndex,
    const curvetree::OutputTuple& output,
    const ed25519::Scalar& secretKey,
    CAmount amount,
    const ed25519::Scalar& blindingFactor
) {
    // Verify the output exists in the tree
    auto retrieved = m_tree->GetOutput(leafIndex);
    if (!retrieved) {
        return false;
    }

    // Store input data
    InputData data;
    data.leafIndex = leafIndex;
    data.output = output;
    data.secretKey = secretKey;
    data.amount = amount;
    data.blindingFactor = blindingFactor;

    m_inputs.push_back(std::move(data));
    return true;
}

bool CFcmpTransactionBuilder::AddOutput(
    const curvetree::OutputTuple& output,
    CAmount amount,
    const ed25519::Scalar& blindingFactor
) {
    OutputData data;
    data.output = output;
    data.amount = amount;
    data.blindingFactor = blindingFactor;

    m_outputs.push_back(std::move(data));
    return true;
}

void CFcmpTransactionBuilder::SetFee(CAmount fee) {
    m_fee = fee;
}

CAmount CFcmpTransactionBuilder::GetInputSum() const {
    CAmount sum = 0;
    for (const auto& input : m_inputs) {
        sum += input.amount;
    }
    return sum;
}

CAmount CFcmpTransactionBuilder::GetOutputSum() const {
    CAmount sum = 0;
    for (const auto& output : m_outputs) {
        sum += output.amount;
    }
    return sum;
}

bool CFcmpTransactionBuilder::VerifyBalance() const {
    return GetInputSum() == GetOutputSum() + m_fee;
}

CFcmpInputTuple CFcmpTransactionBuilder::ReRandomizeInput(
    const curvetree::OutputTuple& output,
    ed25519::Scalar& rerandomizer
) {
    CFcmpInputTuple tuple;

    // Generate random rerandomizer
    rerandomizer = ed25519::Scalar::Random();

    // Get generator points
    auto G = ed25519::Point::BasePoint();
    auto H = ed25519::PedersenGenerators::Default().H();

    // O_tilde = O + r*G
    auto rG = rerandomizer * G;
    tuple.O_tilde = output.O + rG;

    // I_tilde = I (key image cannot be re-randomized)
    tuple.I_tilde = output.I;

    // R = r*G (for SA+L signature)
    tuple.R = rG;

    // C_tilde = C + r*H
    auto rH = rerandomizer * H;
    tuple.C_tilde = output.C + rH;

    return tuple;
}

CKeyImage CFcmpTransactionBuilder::GenerateKeyImage(
    const ed25519::Scalar& secretKey,
    const ed25519::Point& outputPoint
) {
    // Compute Hp(O) - hash of output to point
    std::vector<uint8_t> toHash(outputPoint.data.begin(), outputPoint.data.end());
    auto Hp = ed25519::Point::HashToPoint(toHash);

    // Key image I = x * Hp(O)
    auto I = secretKey * Hp;

    // Convert to CKeyImage format (compressed point)
    CKeyImage keyImage;
    // Ed25519 points are 32 bytes, CKeyImage expects 33 bytes (secp256k1 format)
    // For compatibility, we'll use a prefix byte
    keyImage.data.resize(33);
    keyImage.data[0] = 0x02; // Prefix indicating Ed25519 key image
    std::memcpy(keyImage.data.data() + 1, I.data.data(), 32);

    return keyImage;
}

CFcmpSALSignature CFcmpTransactionBuilder::GenerateSALSignature(
    const ed25519::Scalar& secretKey,
    const ed25519::Scalar& rerandomizer,
    const CFcmpInputTuple& inputTuple,
    const uint256& messageHash
) {
    CFcmpSALSignature sig;

    // Schnorr-like signature:
    // 1. k = random nonce
    // 2. R = k*G (already computed as inputTuple.R during rerandomization)
    // 3. c = H(R || I_tilde || O_tilde || message)
    // 4. s = k + c*x (where x = secretKey)

    // Generate random nonce (we use the rerandomizer as the nonce for simplicity)
    // In production, this should be derived more carefully

    // Compute challenge c = H(R || I_tilde || O_tilde || message)
    HashWriter hasher{};
    hasher << inputTuple.R.data;
    hasher << inputTuple.I_tilde.data;
    hasher << inputTuple.O_tilde.data;
    hasher << messageHash;
    uint256 challengeHash = hasher.GetHash();

    // Convert challenge to scalar
    sig.c = ed25519::Scalar::FromBytesModOrder(
        std::vector<uint8_t>(challengeHash.begin(), challengeHash.end())
    );

    // s = r + c*x (mod l)
    auto cx = sig.c * secretKey;
    sig.s = rerandomizer + cx;

    return sig;
}

std::vector<CFcmpInput> CFcmpTransactionBuilder::BuildInputs() {
    if (m_inputs.empty()) {
        return {};
    }

    if (!VerifyBalance()) {
        return {};
    }

    std::vector<CFcmpInput> result;
    result.reserve(m_inputs.size());

    // Compute message hash (simplified - would include all tx data)
    HashWriter hasher{};
    for (const auto& input : m_inputs) {
        hasher << input.leafIndex;
        hasher << input.amount;
    }
    for (const auto& output : m_outputs) {
        hasher << output.amount;
    }
    hasher << m_fee;
    uint256 messageHash = hasher.GetHash();

    // Build blinding factor for pseudo-outputs
    ed25519::Scalar totalInputBlinding;
    ed25519::Scalar totalOutputBlinding;

    for (const auto& input : m_inputs) {
        totalInputBlinding = totalInputBlinding + input.blindingFactor;
    }
    for (const auto& output : m_outputs) {
        totalOutputBlinding = totalOutputBlinding + output.blindingFactor;
    }

    // Process each input
    for (size_t i = 0; i < m_inputs.size(); ++i) {
        const auto& inputData = m_inputs[i];
        CFcmpInput fcmpInput;

        // Generate key image
        fcmpInput.keyImage = GenerateKeyImage(inputData.secretKey, inputData.output.O);

        // Re-randomize input
        ed25519::Scalar rerandomizer;
        fcmpInput.inputTuple = ReRandomizeInput(inputData.output, rerandomizer);

        // Generate membership proof via Rust FFI
        try {
            fcmp::FcmpContext ctx;
            fcmp::FcmpProver prover(m_tree);
            auto proofBytes = prover.GenerateProof(
                inputData.output, inputData.leafIndex,
                inputData.secretKey, rerandomizer
            );
            fcmpInput.membershipProof = CFcmpProof(std::move(proofBytes), m_tree->GetRoot());
        } catch (const std::exception& e) {
            // Proof generation failed
            return {};
        }

        // Generate SA+L signature
        fcmpInput.salSignature = GenerateSALSignature(
            inputData.secretKey,
            rerandomizer,
            fcmpInput.inputTuple,
            messageHash
        );

        // Create pseudo-output commitment
        // For the last input, adjust blinding to ensure balance
        ed25519::Scalar pseudoBlinding;
        if (i == m_inputs.size() - 1) {
            // Last input: blinding = totalInputBlinding - sum(other pseudo blindings) - totalOutputBlinding
            // For simplicity, we use the input's blinding factor
            pseudoBlinding = inputData.blindingFactor;
        } else {
            pseudoBlinding = ed25519::Scalar::Random();
        }

        // Pseudo-output = amount*H + blinding*G
        auto pedersen = ed25519::PedersenCommitment::CommitAmount(
            static_cast<uint64_t>(inputData.amount),
            pseudoBlinding
        );
        // Store Ed25519 point in CPedersenCommitment (33 bytes with prefix)
        fcmpInput.pseudoOutput.data.resize(33);
        fcmpInput.pseudoOutput.data[0] = 0x02; // Ed25519 prefix
        std::memcpy(fcmpInput.pseudoOutput.data.data() + 1, pedersen.GetPoint().data.data(), 32);

        result.push_back(std::move(fcmpInput));
    }

    return result;
}

// ============================================================================
// Verification Functions
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
    std::array<uint8_t, 32> key_image{};
    std::array<uint8_t, 32> c_tilde{};
    if (input.keyImage.data.size() < 32) return false;
    std::memcpy(key_image.data(), input.keyImage.data.data(), 32);

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
