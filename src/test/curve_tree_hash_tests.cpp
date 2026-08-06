// Copyright (c) 2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Curve-tree hashing on the Selene/Helios cycle.
//
// The tree this replaces hashed ed25519 -> ed25519 by reducing compressed point
// bytes modulo the group order. That is not injective and cannot be opened in
// the proving circuit, so membership proofs over it proved nothing. These tests
// pin the properties a curve tree must actually have.

#include <boost/test/unit_test.hpp>

#include <privacy/curvetree/curve_tree_hash.h>
#include <privacy/ed25519/ed25519_types.h>
#include <privacy/ed25519/pedersen.h>
#include <test/util/setup_common.h>

#include <vector>

using namespace curvetree;

namespace {

//! A leaf whose points are all valid ed25519 curve points. Coordinates get
//! decomposed during hashing, so off-curve bytes are rejected rather than
//! silently producing a root.
OutputTuple MakeOutput(uint64_t seed)
{
    OutputTuple t;
    const ed25519::Scalar a(seed * 7 + 1);
    const ed25519::Scalar b(seed * 13 + 3);
    const ed25519::Scalar c(seed * 29 + 5);
    t.O = a * ed25519::Point::BasePoint();
    t.I = b * ed25519::Point::BasePoint();
    t.C = c * ed25519::Point::BasePoint();
    return t;
}

std::vector<OutputTuple> MakeOutputs(size_t n)
{
    std::vector<OutputTuple> v;
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) v.push_back(MakeOutput(i));
    return v;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(curve_tree_hash_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(widths_come_from_the_crate)
{
    // Hard-coding these in C++ is how the two sides drift apart.
    BOOST_CHECK_EQUAL(LayerWidthSelene(), 38U);
    BOOST_CHECK_EQUAL(LayerWidthHelios(), 18U);
}

BOOST_AUTO_TEST_CASE(leaf_branch_hashes_to_a_selene_node)
{
    auto h = HashLeafBranch(MakeOutputs(3));
    BOOST_REQUIRE(h.has_value());
    BOOST_CHECK(h->curve == TreeCurve::SELENE);

    // Deterministic: the same outputs must always give the same node.
    auto again = HashLeafBranch(MakeOutputs(3));
    BOOST_REQUIRE(again.has_value());
    BOOST_CHECK(*h == *again);
}

BOOST_AUTO_TEST_CASE(leaf_branch_is_sensitive_to_content_and_order)
{
    auto base = HashLeafBranch(MakeOutputs(4));
    BOOST_REQUIRE(base.has_value());

    // Different membership -> different root, or the tree would not bind to the
    // set it claims to contain.
    auto other = HashLeafBranch(MakeOutputs(5));
    BOOST_REQUIRE(other.has_value());
    BOOST_CHECK(*base != *other);

    // Different ORDER -> different root. A tree insensitive to order would let
    // anyone permute a branch and keep the same root.
    auto outs = MakeOutputs(4);
    std::swap(outs[0], outs[1]);
    auto swapped = HashLeafBranch(outs);
    BOOST_REQUIRE(swapped.has_value());
    BOOST_CHECK(*base != *swapped);
}

BOOST_AUTO_TEST_CASE(layers_alternate_between_the_two_curves)
{
    // This alternation IS the curve tree. Selene children must produce a Helios
    // parent and vice versa; hashing a layer into its own curve would break the
    // property that makes the next layer provable.
    auto leaf = HashLeafBranch(MakeOutputs(2));
    BOOST_REQUIRE(leaf.has_value());
    BOOST_CHECK(leaf->curve == TreeCurve::SELENE);

    auto helios = HashBranch({*leaf, *leaf});
    BOOST_REQUIRE(helios.has_value());
    BOOST_CHECK(helios->curve == TreeCurve::HELIOS);

    auto selene = HashBranch({*helios, *helios});
    BOOST_REQUIRE(selene.has_value());
    BOOST_CHECK(selene->curve == TreeCurve::SELENE);
}

BOOST_AUTO_TEST_CASE(mixed_curve_branches_are_refused)
{
    auto leaf = HashLeafBranch(MakeOutputs(2));
    BOOST_REQUIRE(leaf.has_value());
    auto helios = HashBranch({*leaf});
    BOOST_REQUIRE(helios.has_value());

    // A branch's hash function is chosen by its children's curve, so a mixed
    // branch has no defined hash. Refuse rather than pick one arbitrarily.
    BOOST_CHECK(!HashBranch({*leaf, *helios}).has_value());
}

BOOST_AUTO_TEST_CASE(over_wide_branches_are_refused_not_truncated)
{
    // Silently dropping children past the width would produce a root that omits
    // outputs the tree claims to contain.
    BOOST_CHECK(!HashLeafBranch(MakeOutputs(LayerWidthSelene() + 1)).has_value());

    auto leaf = HashLeafBranch(MakeOutputs(1));
    BOOST_REQUIRE(leaf.has_value());
    std::vector<TreeHash> too_many(LayerWidthHelios() + 1, *leaf);
    BOOST_CHECK(!HashBranch(too_many).has_value());

    BOOST_CHECK(!HashLeafBranch({}).has_value());
    BOOST_CHECK(!HashBranch({}).has_value());
}

BOOST_AUTO_TEST_CASE(root_spans_multiple_layers)
{
    // One full leaf branch: still a single Selene node, no layer above it.
    auto one_layer = ComputeRoot(MakeOutputs(LayerWidthSelene()));
    BOOST_REQUIRE(one_layer.has_value());
    BOOST_CHECK(one_layer->curve == TreeCurve::SELENE);

    // Past one branch, a Helios layer must form above the Selene leaves.
    auto two_layer = ComputeRoot(MakeOutputs(LayerWidthSelene() + 1));
    BOOST_REQUIRE(two_layer.has_value());
    BOOST_CHECK(two_layer->curve == TreeCurve::HELIOS);
    BOOST_CHECK(*one_layer != *two_layer);

    // Growing the set must move the root; a tree whose root ignored later
    // outputs could not prove they are members.
    auto bigger = ComputeRoot(MakeOutputs(LayerWidthSelene() * 2 + 3));
    BOOST_REQUIRE(bigger.has_value());
    BOOST_CHECK(*bigger != *two_layer);
}

BOOST_AUTO_TEST_CASE(root_is_deterministic_and_empty_has_none)
{
    auto a = ComputeRoot(MakeOutputs(50));
    auto b = ComputeRoot(MakeOutputs(50));
    BOOST_REQUIRE(a.has_value());
    BOOST_REQUIRE(b.has_value());
    BOOST_CHECK(*a == *b);

    // An empty set has no root. Handing back a zero value would look like a
    // real root and match nothing.
    BOOST_CHECK(!ComputeRoot({}).has_value());
}

BOOST_AUTO_TEST_SUITE_END()
