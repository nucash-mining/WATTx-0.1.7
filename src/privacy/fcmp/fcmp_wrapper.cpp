// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <privacy/fcmp/fcmp_wrapper.h>

#include <cstring>

namespace privacy {
namespace fcmp {

std::vector<uint8_t> FcmpProver::GenerateProof(
    const curvetree::OutputTuple& output,
    uint64_t leaf_index,
    const ed25519::Scalar& secret_key,
    const ed25519::Scalar& rerandomizer
) {
    // Get the branch (Merkle path) for this output
    auto branch_opt = m_tree->GetBranch(leaf_index);
    if (!branch_opt) {
        throw FcmpError(FCMP_ERROR_INVALID_PARAM, "Failed to get branch for leaf index");
    }
    const curvetree::TreeBranch& branch = *branch_opt;

    // Get tree root
    const curvetree::TreeHash root = m_tree->GetRoot();

    // Prepare output tuple (O || I || C = 96 bytes)
    std::vector<uint8_t> output_bytes(FCMP_OUTPUT_TUPLE_SIZE);
    std::memcpy(output_bytes.data(), output.O.data.data(), FCMP_POINT_SIZE);
    std::memcpy(output_bytes.data() + FCMP_POINT_SIZE, output.I.data.data(), FCMP_POINT_SIZE);
    std::memcpy(output_bytes.data() + 2 * FCMP_POINT_SIZE, output.C.data.data(), FCMP_POINT_SIZE);

    // Convert branch to FFI format
    std::vector<FcmpBranchLayer> layers;
    std::vector<std::vector<uint8_t>> layer_elements;

    for (const auto& level : branch.layers) {
        // Serialize all siblings at this level (each is a Scalar)
        std::vector<uint8_t> elements;
        for (const auto& sibling : level) {
            elements.insert(elements.end(), sibling.bytes.begin(), sibling.bytes.end());
        }
        layer_elements.push_back(std::move(elements));
    }

    // Build FcmpBranchLayer array (after layer_elements is stable)
    layers.reserve(branch.layers.size());
    for (size_t i = 0; i < branch.layers.size(); ++i) {
        FcmpBranchLayer layer;
        layer.num_elements = static_cast<uint32_t>(branch.layers[i].size());
        layer.elements = layer_elements[i].data();
        layers.push_back(layer);
    }

    // Build FcmpBranch
    FcmpBranch fcmp_branch;
    fcmp_branch.leaf_index = leaf_index;
    fcmp_branch.num_layers = static_cast<uint32_t>(layers.size());
    fcmp_branch.layers = layers.data();

    // Allocate proof buffer
    size_t max_proof_size = EstimateProofSize(1);
    std::vector<uint8_t> proof(max_proof_size);
    size_t actual_size = 0;

    // Generate proof with secret key and rerandomizer
    int32_t result = fcmp_prove(
        proof.data(),
        &actual_size,
        max_proof_size,
        root.bytes.data(),
        output_bytes.data(),
        &fcmp_branch,
        secret_key.data.data(),
        rerandomizer.data.data()
    );

    if (result != FCMP_SUCCESS) {
        throw FcmpError(result);
    }

    proof.resize(actual_size);
    return proof;
}

bool FcmpVerifier::Verify(const FcmpInput& input, const std::vector<uint8_t>& proof) const {
    int32_t result = fcmp_verify(
        m_tree_root.bytes.data(),
        &input,
        proof.data(),
        proof.size()
    );

    return result == FCMP_SUCCESS;
}

} // namespace fcmp
} // namespace privacy

// ============================================================================
// Real FCMP++ proving / verification
// ============================================================================
//
// These call fcmp_prove_full / fcmp_verify_full, the audited path that binds the
// re-randomised commitment C~ to the leaf's own C. The older fcmp_prove is a
// Schnorr-sigma scaffold that proves nothing about C, so a proof from it cannot
// support value conservation.

namespace privacy {
namespace fcmp {

// The prover needs the whole leaf branch and our position in it -- it rebuilds
// the leaf hash itself, so sibling scalars would not do. Both halves of a spend
// need the identical branch, so they collect it the same way.
static std::vector<uint8_t> CollectLeafBranch(
    const std::shared_ptr<curvetree::CurveTree>& tree,
    uint64_t leaf_index,
    size_t& num_leaves_out,
    size_t& index_in_leaves_out)
{
    if (!tree) {
        throw FcmpError(FCMP_ERROR_INVALID_PARAM, "No curve tree");
    }

    auto branch_opt = tree->GetBranch(leaf_index);
    if (!branch_opt) {
        throw FcmpError(FCMP_ERROR_INVALID_PARAM, "No branch for leaf index");
    }
    const curvetree::TreeBranch& branch = *branch_opt;
    if (branch.leaves.empty()) {
        throw FcmpError(FCMP_ERROR_INVALID_PARAM, "Branch carries no leaves");
    }
    if (branch.index_in_leaves >= branch.leaves.size()) {
        throw FcmpError(FCMP_ERROR_INVALID_PARAM, "Leaf index outside its branch");
    }

    // fcmp_prove_full is single-layer today: it takes the leaf branch and no
    // higher layers. Refuse rather than hand it a branch it will silently
    // ignore, which would produce a proof against the wrong root.
    if (!branch.layers.empty()) {
        throw FcmpError(FCMP_ERROR_INVALID_PARAM,
                        "Multi-layer branches are not yet supported by fcmp_prove_full");
    }

    std::vector<uint8_t> leaves;
    leaves.reserve(branch.leaves.size() * 96);
    for (const auto& o : branch.leaves) {
        leaves.insert(leaves.end(), o.O.data.begin(), o.O.data.end());
        leaves.insert(leaves.end(), o.I.data.begin(), o.I.data.end());
        leaves.insert(leaves.end(), o.C.data.begin(), o.C.data.end());
    }

    num_leaves_out = branch.leaves.size();
    index_in_leaves_out = branch.index_in_leaves;
    return leaves;
}

FcmpProver::Rerandomization FcmpProver::Rerandomize(uint64_t leaf_index)
{
    size_t num_leaves = 0;
    size_t index_in_leaves = 0;
    const std::vector<uint8_t> leaves =
        CollectLeafBranch(m_tree, leaf_index, num_leaves, index_in_leaves);

    Rerandomization out;
    out.leaf_index = leaf_index;
    out.state.resize(256);
    size_t state_len = 0;

    const int32_t rc = fcmp_rerandomize(
        leaves.data(), num_leaves, index_in_leaves,
        out.state.data(), out.state.size(), &state_len,
        out.c_tilde.data(), out.c_blind.data());

    if (rc != FCMP_SUCCESS) {
        throw FcmpError(rc, "fcmp_rerandomize failed");
    }
    out.state.resize(state_len);
    return out;
}

std::vector<uint8_t> FcmpProver::GenerateFullProof(
    const Rerandomization& rerandomized,
    const ed25519::Scalar& x,
    const ed25519::Scalar& y,
    const uint256& signable_tx_hash,
    std::array<uint8_t, 32>& key_image_out)
{
    if (rerandomized.state.empty()) {
        throw FcmpError(FCMP_ERROR_INVALID_PARAM,
                        "Re-randomize the leaf before proving it");
    }

    // Same leaf, same branch as Rerandomize() saw. A different branch here would
    // make the SA+L proof and the membership proof speak about different outputs.
    size_t num_leaves = 0;
    size_t index_in_leaves = 0;
    const std::vector<uint8_t> leaves =
        CollectLeafBranch(m_tree, rerandomized.leaf_index, num_leaves, index_in_leaves);

    const std::vector<uint8_t> xb = x.GetBytes();
    const std::vector<uint8_t> yb = y.GetBytes();
    if (xb.size() != 32 || yb.size() != 32) {
        throw FcmpError(FCMP_ERROR_INVALID_SCALAR, "Spend key components are not 32 bytes");
    }

    std::vector<uint8_t> proof(64 * 1024);
    size_t proof_len = 0;

    const int32_t rc = fcmp_prove_full(
        proof.data(), &proof_len, proof.size(),
        leaves.data(), num_leaves, index_in_leaves,
        xb.data(), yb.data(),
        signable_tx_hash.begin(),
        rerandomized.state.data(), rerandomized.state.size(),
        key_image_out.data());

    if (rc != FCMP_SUCCESS) {
        throw FcmpError(rc, "fcmp_prove_full failed");
    }
    proof.resize(proof_len);
    return proof;
}

bool FcmpVerifier::VerifyFull(const std::vector<uint8_t>& proof,
                              const std::array<uint8_t, 32>& key_image,
                              const std::array<uint8_t, 32>& c_tilde,
                              const uint256& signable_tx_hash,
                              size_t num_layers) const
{
    if (proof.empty() || num_layers == 0) return false;

    // A single-layer tree's root is a Selene point. Verifying against a root
    // whose curve does not match the layer count would compare against the
    // wrong group entirely.
    const bool expect_selene = (num_layers % 2) == 1;
    if (expect_selene && m_tree_root.curve != curvetree::TreeCurve::SELENE) return false;
    if (!expect_selene && m_tree_root.curve != curvetree::TreeCurve::HELIOS) return false;

    return fcmp_verify_full(m_tree_root.bytes.data(), num_layers,
                            proof.data(), proof.size(),
                            key_image.data(), c_tilde.data(),
                            signable_tx_hash.begin()) == FCMP_SUCCESS;
}

} // namespace fcmp
} // namespace privacy
