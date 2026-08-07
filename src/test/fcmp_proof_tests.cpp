// Copyright (c) 2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// CFcmpProof v2 and prover-compatible tree roots.
//
// Two things are checked here, and the first is the one that decides whether any
// of the FCMP work means anything:
//
//   1. The root our CurveTree computes must equal the root the audited prover
//      computes for the same outputs. If they differ, every membership proof is
//      verified against a root nothing was proven against, and the whole
//      construction is decorative.
//
//   2. CFcmpProof must round-trip its curve-TAGGED root, and must refuse a v1
//      proof. v1 carried a bare ed25519 point, which could only ever name a root
//      from the old ed25519 -> ed25519 tree -- a hash that is not injective and
//      cannot be opened inside the proving circuit.

#include <boost/test/unit_test.hpp>

#include <privacy/curvetree/curve_tree.h>
#include <privacy/curvetree/curve_tree_hash.h>
#include <privacy/fcmp/fcmp_ffi.h>
#include <privacy/fcmp_tx.h>
#include <privacy/ed25519/ed25519_types.h>
#include <streams.h>
#include <test/util/setup_common.h>

#include <vector>

using namespace curvetree;

namespace {

curvetree::OutputTuple MakeOutput(uint64_t seed)
{
    curvetree::OutputTuple t;
    t.O = ed25519::Scalar(seed * 7 + 1) * ed25519::Point::BasePoint();
    t.I = ed25519::Scalar(seed * 13 + 3) * ed25519::Point::BasePoint();
    t.C = ed25519::Scalar(seed * 29 + 5) * ed25519::Point::BasePoint();
    return t;
}

std::vector<curvetree::OutputTuple> MakeOutputs(size_t n)
{
    std::vector<curvetree::OutputTuple> v;
    for (size_t i = 0; i < n; ++i) v.push_back(MakeOutput(i));
    return v;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(fcmp_proof_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(tree_root_matches_what_the_prover_computes)
{
    // THE test. fcmp_compute_leaf_root is the crate's own leaf-root routine --
    // the same one fcmp_prove_full/fcmp_verify_full agree on. If our stored tree
    // disagrees with it, proofs verify against a root nobody proved against.
    const size_t n = 12;
    auto outs = MakeOutputs(n);

    auto storage = std::make_shared<MemoryTreeStorage>();
    CurveTree tree(storage);
    tree.AddOutputs(outs);
    BOOST_REQUIRE_EQUAL(tree.GetOutputCount(), n);

    const TreeHash ours = tree.GetRoot();
    BOOST_CHECK(ours.curve == TreeCurve::SELENE);

    // Independently compute the same root straight from the crate.
    std::vector<uint8_t> flat;
    for (const auto& o : outs) {
        flat.insert(flat.end(), o.O.data.begin(), o.O.data.end());
        flat.insert(flat.end(), o.I.data.begin(), o.I.data.end());
        flat.insert(flat.end(), o.C.data.begin(), o.C.data.end());
    }
    std::array<uint8_t, 32> reference{};
    BOOST_REQUIRE_EQUAL(fcmp_compute_leaf_root(reference.data(), flat.data(), n), FCMP_SUCCESS);

    BOOST_CHECK_MESSAGE(ours.bytes == reference,
        "CurveTree root does not match the prover's root for the same outputs");
}

BOOST_AUTO_TEST_CASE(root_tracks_the_output_set)
{
    auto storage = std::make_shared<MemoryTreeStorage>();
    CurveTree tree(storage);

    // An empty tree has no root. A placeholder would look real and match nothing.
    const TreeHash empty{};
    BOOST_CHECK(tree.GetRoot() == empty);

    tree.AddOutputs(MakeOutputs(4));
    const TreeHash a = tree.GetRoot();
    BOOST_CHECK(a != empty);

    // Adding an output must move the root, or the tree could not prove the new
    // output is a member.
    tree.AddOutputs({MakeOutput(99)});
    const TreeHash b = tree.GetRoot();
    BOOST_CHECK(a != b);
}

BOOST_AUTO_TEST_CASE(proof_round_trips_its_tagged_root)
{
    TreeHash root;
    root.curve = TreeCurve::HELIOS;
    root.bytes.fill(0xAB);

    privacy::CFcmpProof proof(std::vector<uint8_t>{1, 2, 3, 4}, root);
    BOOST_CHECK_EQUAL(proof.version, 2);
    BOOST_CHECK(proof.IsValid());

    DataStream ss;
    ss << proof;
    privacy::CFcmpProof decoded;
    ss >> decoded;

    // The CURVE must survive serialization. Losing it leaves 32 bytes that are a
    // valid point on either curve and hash differently on each -- a root that
    // cannot be checked against anything.
    BOOST_CHECK(decoded.treeRoot.curve == TreeCurve::HELIOS);
    BOOST_CHECK(decoded.treeRoot.bytes == root.bytes);
    BOOST_CHECK_EQUAL(decoded.version, 2);
    BOOST_CHECK(decoded.proofData == proof.proofData);
}

BOOST_AUTO_TEST_CASE(selene_and_helios_roots_stay_distinguishable)
{
    // Same 32 bytes, different curve, must not compare equal -- otherwise a root
    // from one curve would satisfy a check meant for the other.
    TreeHash sel, hel;
    sel.curve = TreeCurve::SELENE;
    hel.curve = TreeCurve::HELIOS;
    sel.bytes.fill(0x11);
    hel.bytes.fill(0x11);
    BOOST_CHECK(sel != hel);

    privacy::CFcmpProof p1(std::vector<uint8_t>{9}, sel);
    privacy::CFcmpProof p2(std::vector<uint8_t>{9}, hel);
    DataStream a, b;
    a << p1;
    b << p2;
    BOOST_CHECK(a.size() == b.size());
    BOOST_CHECK(!std::equal(a.begin(), a.end(), b.begin()));
}

BOOST_AUTO_TEST_CASE(version_one_proofs_are_refused)
{
    TreeHash root;
    root.curve = TreeCurve::SELENE;
    root.bytes.fill(0x7C);

    privacy::CFcmpProof proof(std::vector<uint8_t>{1, 2, 3}, root);
    BOOST_CHECK(proof.IsValid());

    // A v1 proof named a root from the old ed25519 tree, whose hash proves
    // nothing. Refuse it rather than read it with the wrong root type.
    proof.version = 1;
    BOOST_CHECK(!proof.IsValid());

    // An all-zero root is what an empty tree yields; it names nothing.
    privacy::CFcmpProof zero_root(std::vector<uint8_t>{1, 2, 3}, TreeHash{});
    BOOST_CHECK(!zero_root.IsValid());

    // And a proof with no proof bytes is not a proof.
    privacy::CFcmpProof no_data(std::vector<uint8_t>{}, root);
    BOOST_CHECK(!no_data.IsValid());
}

BOOST_AUTO_TEST_SUITE_END()
