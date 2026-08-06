// Copyright (c) 2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_PRIVACY_CURVETREE_CURVE_TREE_HASH_H
#define WATTX_PRIVACY_CURVETREE_CURVE_TREE_HASH_H

// Curve-tree hashing on the Selene/Helios cycle.
//
// WHY THIS REPLACES THE OLD HASHING
// ---------------------------------
// CurveTree::ComputeNodeHash hashed ed25519 -> ed25519 by reducing each child's
// compressed bytes modulo the group order and Pedersen-hashing the result. That
// is not a curve-tree hash:
//
//   * it is not injective -- distinct points collide after reduction, so
//     distinct branches can share a root;
//   * it cannot be opened inside the Generalized Bulletproofs circuit, which is
//     the entire purpose of a curve tree.
//
// So membership proofs over that tree prove nothing, and anything resting on
// them -- including value conservation -- rests on nothing.
//
// FCMP++ needs two curves whose scalar and base fields interlock (a 2-cycle),
// because a statement about one curve's coordinates is only efficiently
// provable in a circuit over the other's scalar field. Layers alternate:
//
//   leaves        : ed25519 outputs -> 6 Selene scalars each -> SELENE point
//   Selene layer  : Selene points -> x coord as Helios scalar -> HELIOS point
//   Helios layer  : Helios points -> x coord as Selene scalar -> SELENE point
//
// All hashing is delegated to the audited fcmp++ crate through the FFI, so the
// tree is hashed by the same code the prover uses rather than a reimplementation.

#include <privacy/curvetree/curve_tree.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace curvetree {

//! Which curve a tree node lives on. A node is meaningless without this: the
//! same 32 bytes are a valid point on either curve and hash differently.
enum class TreeCurve : uint8_t {
    SELENE = 1,
    HELIOS = 2,
};

//! One node of the curve tree: a compressed point plus the curve it belongs to.
struct TreeHash {
    TreeCurve curve{TreeCurve::SELENE};
    std::array<uint8_t, 32> bytes{};

    bool operator==(const TreeHash& o) const {
        return curve == o.curve && bytes == o.bytes;
    }
    bool operator!=(const TreeHash& o) const { return !(*this == o); }
};

//! Branch widths, read from the crate rather than hard-coded here.
size_t LayerWidthSelene();  //!< children per Selene (C1) node
size_t LayerWidthHelios();  //!< children per Helios (C2) node

/**
 * Hash one leaf branch (up to LayerWidthSelene() outputs) into its Selene parent.
 *
 * Each output contributes six Selene scalars: the x and y coordinates of O, I
 * and C. Unlike internal layers, both coordinates are used.
 */
std::optional<TreeHash> HashLeafBranch(const std::vector<OutputTuple>& outputs);

/**
 * Hash one branch of internal nodes into its parent.
 *
 * The parent's curve is determined by the children's: Selene children produce a
 * Helios parent and vice versa. All children must be on the same curve, and the
 * branch must not exceed that curve's width.
 */
std::optional<TreeHash> HashBranch(const std::vector<TreeHash>& children);

/**
 * Compute the root over a complete output set.
 *
 * Builds the leaf layer, then repeatedly hashes each layer into the next until
 * a single node remains. Returns nullopt if any layer fails to hash.
 *
 * An empty output set has no root -- callers must handle that case explicitly
 * rather than being handed a zero value that looks like a real root.
 */
std::optional<TreeHash> ComputeRoot(const std::vector<OutputTuple>& outputs);

} // namespace curvetree

#endif // WATTX_PRIVACY_CURVETREE_CURVE_TREE_HASH_H
