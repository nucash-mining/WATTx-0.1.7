// Copyright (c) 2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <privacy/curvetree/curve_tree_hash.h>

#include <privacy/fcmp/fcmp_ffi.h>
#include <logging.h>

#include <cstring>

namespace curvetree {

size_t LayerWidthSelene() { return fcmp_layer_one_len(); }
size_t LayerWidthHelios() { return fcmp_layer_two_len(); }

std::optional<TreeHash> HashLeafBranch(const std::vector<OutputTuple>& outputs)
{
    if (outputs.empty()) return std::nullopt;
    if (outputs.size() > LayerWidthSelene()) {
        LogPrintf("CurveTree: leaf branch of %zu exceeds the %zu-output width\n",
                  outputs.size(), LayerWidthSelene());
        return std::nullopt;
    }

    // O || I || C per output, 96 bytes each -- the layout fcmp_compute_leaf_root
    // expects.
    std::vector<uint8_t> flat;
    flat.reserve(outputs.size() * 96);
    for (const auto& o : outputs) {
        flat.insert(flat.end(), o.O.data.begin(), o.O.data.end());
        flat.insert(flat.end(), o.I.data.begin(), o.I.data.end());
        flat.insert(flat.end(), o.C.data.begin(), o.C.data.end());
    }

    TreeHash out;
    out.curve = TreeCurve::SELENE;
    const int32_t rc = fcmp_compute_leaf_root(out.bytes.data(), flat.data(), outputs.size());
    if (rc != FCMP_SUCCESS) {
        LogPrintf("CurveTree: leaf branch hashing failed (%d)\n", rc);
        return std::nullopt;
    }
    return out;
}

std::optional<TreeHash> HashBranch(const std::vector<TreeHash>& children)
{
    if (children.empty()) return std::nullopt;

    // Mixing curves within a branch is never valid: the parent's hash function
    // is chosen by the children's curve, so a mixed branch has no defined hash.
    const TreeCurve child_curve = children.front().curve;
    for (const auto& c : children) {
        if (c.curve != child_curve) {
            LogPrintf("CurveTree: branch mixes Selene and Helios children\n");
            return std::nullopt;
        }
    }

    const size_t width = (child_curve == TreeCurve::SELENE) ? LayerWidthHelios()
                                                            : LayerWidthSelene();
    if (children.size() > width) {
        LogPrintf("CurveTree: branch of %zu exceeds the %zu-child width for this layer\n",
                  children.size(), width);
        return std::nullopt;
    }

    std::vector<uint8_t> flat;
    flat.reserve(children.size() * 32);
    for (const auto& c : children) {
        flat.insert(flat.end(), c.bytes.begin(), c.bytes.end());
    }

    TreeHash out;
    int32_t rc;
    if (child_curve == TreeCurve::SELENE) {
        // Selene children hash into a Helios parent.
        out.curve = TreeCurve::HELIOS;
        rc = fcmp_hash_helios_layer(out.bytes.data(), flat.data(), children.size());
    } else {
        out.curve = TreeCurve::SELENE;
        rc = fcmp_hash_selene_layer(out.bytes.data(), flat.data(), children.size());
    }
    if (rc != FCMP_SUCCESS) {
        LogPrintf("CurveTree: branch hashing failed (%d)\n", rc);
        return std::nullopt;
    }
    return out;
}

std::optional<TreeHash> ComputeRoot(const std::vector<OutputTuple>& outputs)
{
    // No outputs means no root. Returning a zero value here would hand callers
    // something that looks like a root and matches nothing.
    if (outputs.empty()) return std::nullopt;

    // Leaf layer: chunk the outputs into leaf branches.
    std::vector<TreeHash> layer;
    const size_t leaf_width = LayerWidthSelene();
    for (size_t i = 0; i < outputs.size(); i += leaf_width) {
        const size_t n = std::min(leaf_width, outputs.size() - i);
        std::vector<OutputTuple> chunk(outputs.begin() + i, outputs.begin() + i + n);
        auto h = HashLeafBranch(chunk);
        if (!h) return std::nullopt;
        layer.push_back(*h);
    }

    // Hash upward until one node remains. Each pass alternates curve, so the
    // width alternates with it.
    while (layer.size() > 1) {
        const size_t width = (layer.front().curve == TreeCurve::SELENE)
                                 ? LayerWidthHelios()
                                 : LayerWidthSelene();
        std::vector<TreeHash> next;
        next.reserve((layer.size() + width - 1) / width);
        for (size_t i = 0; i < layer.size(); i += width) {
            const size_t n = std::min(width, layer.size() - i);
            std::vector<TreeHash> chunk(layer.begin() + i, layer.begin() + i + n);
            auto h = HashBranch(chunk);
            if (!h) return std::nullopt;
            next.push_back(*h);
        }
        layer = std::move(next);
    }

    return layer.front();
}

} // namespace curvetree
