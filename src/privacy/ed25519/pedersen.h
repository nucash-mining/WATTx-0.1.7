// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_PRIVACY_ED25519_PEDERSEN_H
#define WATTX_PRIVACY_ED25519_PEDERSEN_H

#include <privacy/ed25519/ed25519_types.h>

#include <vector>
#include <memory>

namespace ed25519 {

/**
 * Pedersen Commitment over Ed25519
 *
 * A Pedersen commitment to value v with blinding factor r is:
 *   C = v*G + r*H
 *
 * Where G is the base point and H is a nothing-up-my-sleeve point
 * derived by hashing G.
 *
 * Properties:
 * - Perfectly hiding: C reveals nothing about v
 * - Computationally binding: cannot find v', r' such that C = v'*G + r'*H
 * - Homomorphic: C1 + C2 = (v1+v2)*G + (r1+r2)*H
 */
class PedersenCommitment {
public:
    Point commitment;

    PedersenCommitment() = default;
    explicit PedersenCommitment(const Point& p) : commitment(p) {}

    // Commit to a value with random blinding factor
    static PedersenCommitment Commit(const Scalar& value);

    // Commit to a value with specified blinding factor
    static PedersenCommitment Commit(const Scalar& value, const Scalar& blinding);

    // Commit to amount (64-bit) with random blinding
    static PedersenCommitment CommitAmount(uint64_t amount);

    // Commit to amount with specified blinding
    static PedersenCommitment CommitAmount(uint64_t amount, const Scalar& blinding);

    // Homomorphic addition
    PedersenCommitment operator+(const PedersenCommitment& other) const;
    PedersenCommitment operator-(const PedersenCommitment& other) const;
    PedersenCommitment& operator+=(const PedersenCommitment& other);
    PedersenCommitment& operator-=(const PedersenCommitment& other);

    // Scalar multiplication
    PedersenCommitment operator*(const Scalar& s) const;

    bool operator==(const PedersenCommitment& other) const;
    bool operator!=(const PedersenCommitment& other) const { return !(*this == other); }

    // Get the underlying point
    const Point& GetPoint() const { return commitment; }

    // Serialization
    std::vector<uint8_t> GetBytes() const { return commitment.GetBytes(); }
    std::string GetHex() const { return commitment.GetHex(); }
};

/**
 * Pedersen Vector Commitment (PVC)
 *
 * Commits to a vector of scalars (v1, v2, ..., vn) with a single blinding factor:
 *   C = v1*G1 + v2*G2 + ... + vn*Gn + r*H
 *
 * Where G1, G2, ..., Gn are orthogonal generators derived from G.
 * Used in FCMP for committing to branch elements.
 */
class PedersenVectorCommitment {
public:
    Point commitment;

    PedersenVectorCommitment() = default;
    explicit PedersenVectorCommitment(const Point& p) : commitment(p) {}

    // Commit to a vector of values with random blinding
    static PedersenVectorCommitment Commit(const std::vector<Scalar>& values);

    // Commit to a vector of values with specified blinding
    static PedersenVectorCommitment Commit(const std::vector<Scalar>& values, const Scalar& blinding);

    // Homomorphic operations
    PedersenVectorCommitment operator+(const PedersenVectorCommitment& other) const;
    PedersenVectorCommitment operator-(const PedersenVectorCommitment& other) const;

    bool operator==(const PedersenVectorCommitment& other) const;
    bool operator!=(const PedersenVectorCommitment& other) const { return !(*this == other); }

    const Point& GetPoint() const { return commitment; }
    std::vector<uint8_t> GetBytes() const { return commitment.GetBytes(); }
};

/**
 * Generator set for Pedersen commitments
 *
 * Holds the base generators G and H, plus vector generators G_bold for PVC.
 * Generators are derived deterministically using hash-to-curve.
 */
class PedersenGenerators {
public:
    // Initialize with default generators (derived from nothing-up-my-sleeve string)
    PedersenGenerators();

    // Initialize with custom seed
    explicit PedersenGenerators(const std::string& seed);

    // Get the value generator G (Ed25519 base point)
    const Point& G() const { return m_G; }

    // Get the blinding generator H
    const Point& H() const { return m_H; }

    // Get the i-th vector generator
    const Point& G_bold(size_t i) const;

    // Ensure at least n vector generators are available
    void EnsureGenerators(size_t n);

    // Get current count of vector generators
    size_t Size() const { return m_G_bold.size(); }

    // Singleton instance with default generators
    static PedersenGenerators& Default();

private:
    void DeriveGenerators(size_t n);

    Point m_G;  // Value generator (base point)
    Point m_H;  // Blinding generator
    std::vector<Point> m_G_bold;  // Vector generators
    std::string m_seed;
};

/**
 * Opening proof for Pedersen commitment
 * Proves knowledge of (v, r) such that C = v*G + r*H
 */
struct PedersenOpening {
    Scalar value;
    Scalar blinding;

    PedersenOpening() = default;
    PedersenOpening(const Scalar& v, const Scalar& b) : value(v), blinding(b) {}

    // Verify that this opening matches the commitment
    bool Verify(const PedersenCommitment& commitment) const;

    // Verify with explicit generators
    bool Verify(const PedersenCommitment& commitment, const PedersenGenerators& gens) const;

    void Clear() {
        value.Clear();
        blinding.Clear();
    }
};

/**
 * Opening for vector commitment
 */
struct PedersenVectorOpening {
    std::vector<Scalar> values;
    Scalar blinding;

    bool Verify(const PedersenVectorCommitment& commitment) const;
    bool Verify(const PedersenVectorCommitment& commitment, const PedersenGenerators& gens) const;

    void Clear() {
        for (auto& v : values) v.Clear();
        blinding.Clear();
    }
};

/**
 * Pedersen hash for curve trees
 *
 * Hash function: H(x1, x2, ..., xn) = H_init + x1*G1 + x2*G2 + ... + xn*Gn
 *
 * Where H_init is a fixed initialization point to prevent zero inputs
 * from hashing to identity.
 */
class PedersenHash {
public:
    // Initialize with default parameters
    PedersenHash();

    // Initialize with custom seed
    explicit PedersenHash(const std::string& seed);

    // Hash a vector of scalars
    Point Hash(const std::vector<Scalar>& inputs) const;

    // Incremental hash: add more elements to existing hash
    Point HashGrow(const Point& existing, size_t offset,
                   const Scalar& existing_at_offset,
                   const std::vector<Scalar>& new_elements) const;

    // Remove elements and add back one
    Point HashTrim(const Point& existing, size_t offset,
                   const std::vector<Scalar>& elements_to_remove,
                   const Scalar& element_to_grow_back) const;

    // Get the initialization point
    const Point& GetInit() const { return m_init; }

    // Get generators
    const PedersenGenerators& GetGenerators() const { return m_generators; }

private:
    Point m_init;  // Initialization point (prevents zero-input → identity)
    PedersenGenerators m_generators;
};

} // namespace ed25519

#endif // WATTX_PRIVACY_ED25519_PEDERSEN_H
