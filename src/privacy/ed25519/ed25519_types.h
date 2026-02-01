// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_PRIVACY_ED25519_TYPES_H
#define WATTX_PRIVACY_ED25519_TYPES_H

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <serialize.h>

namespace ed25519 {

/** Size constants for Ed25519 */
constexpr size_t SCALAR_SIZE = 32;
constexpr size_t POINT_SIZE = 32;
constexpr size_t SEED_SIZE = 32;
constexpr size_t SIGNATURE_SIZE = 64;

/**
 * Ed25519 curve order (little-endian):
 * l = 2^252 + 27742317777372353535851937790883648493
 */
constexpr std::array<uint8_t, 32> CURVE_ORDER = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};

/**
 * Ed25519 scalar (32 bytes, little-endian)
 * Represents an integer mod l (the curve order)
 */
class Scalar {
public:
    std::array<uint8_t, SCALAR_SIZE> data;

    Scalar() { data.fill(0); }
    explicit Scalar(const std::array<uint8_t, SCALAR_SIZE>& d) : data(d) {}
    explicit Scalar(const uint8_t* d) { std::memcpy(data.data(), d, SCALAR_SIZE); }
    explicit Scalar(uint64_t value);

    // Zero scalar
    static Scalar Zero() { return Scalar(); }

    // One scalar
    static Scalar One();

    // Random scalar
    static Scalar Random();

    // From bytes (reduces mod l)
    static Scalar FromBytesModOrder(const uint8_t* bytes, size_t len);
    static Scalar FromBytesModOrder(const std::vector<uint8_t>& bytes);

    // Arithmetic operations
    Scalar operator+(const Scalar& other) const;
    Scalar operator-(const Scalar& other) const;
    Scalar operator*(const Scalar& other) const;
    Scalar operator-() const;

    Scalar& operator+=(const Scalar& other);
    Scalar& operator-=(const Scalar& other);
    Scalar& operator*=(const Scalar& other);

    // Comparison
    bool operator==(const Scalar& other) const;
    bool operator!=(const Scalar& other) const { return !(*this == other); }

    // Check if zero
    bool IsZero() const;

    // Multiplicative inverse (returns zero if this is zero)
    Scalar Invert() const;

    // Serialization
    std::vector<uint8_t> GetBytes() const;
    std::string GetHex() const;

    // Securely clear memory
    void Clear();

    // Check if null (alias for IsZero)
    bool IsNull() const { return IsZero(); }

    // Serialization
    SERIALIZE_METHODS(Scalar, obj) {
        READWRITE(obj.data);
    }
};

/**
 * Ed25519 point (32 bytes compressed)
 * Represents a point on the Ed25519 curve
 */
class Point {
public:
    std::array<uint8_t, POINT_SIZE> data;

    Point() { data.fill(0); }
    explicit Point(const std::array<uint8_t, POINT_SIZE>& d) : data(d) {}
    explicit Point(const uint8_t* d) { std::memcpy(data.data(), d, POINT_SIZE); }

    // Identity point (neutral element)
    static Point Identity();

    // Base point (generator G)
    static Point BasePoint();

    // Alias for BasePoint
    static Point Generator() { return BasePoint(); }

    // Random point (random scalar * G)
    static Point Random();

    // Hash to point (Elligator or similar)
    static Point HashToPoint(const uint8_t* data, size_t len);
    static Point HashToPoint(const std::vector<uint8_t>& data);

    // Point arithmetic
    Point operator+(const Point& other) const;
    Point operator-(const Point& other) const;
    Point operator-() const;

    Point& operator+=(const Point& other);
    Point& operator-=(const Point& other);

    // Scalar multiplication
    Point operator*(const Scalar& scalar) const;

    // Comparison
    bool operator==(const Point& other) const;
    bool operator!=(const Point& other) const { return !(*this == other); }

    // Check if identity
    bool IsIdentity() const;

    // Check if valid point on curve
    bool IsValid() const;

    // Serialization
    std::vector<uint8_t> GetBytes() const;
    std::string GetHex() const;

    // Get x and y coordinates (for Pedersen hash)
    bool GetXY(Scalar& x, Scalar& y) const;

    // Securely clear memory
    void Clear();

    // Check if null (alias for !IsValid)
    bool IsNull() const { return !IsValid(); }

    // Serialization
    SERIALIZE_METHODS(Point, obj) {
        READWRITE(obj.data);
    }
};

// Scalar * Point multiplication (commutative notation)
inline Point operator*(const Scalar& s, const Point& p) { return p * s; }

/**
 * Multi-scalar multiplication (MSM)
 * Computes: sum(scalars[i] * points[i])
 * More efficient than individual multiplications
 */
Point MultiScalarMul(const std::vector<Scalar>& scalars, const std::vector<Point>& points);

/**
 * Double scalar multiplication
 * Computes: a*G + b*P where G is base point
 * Used for signature verification
 */
Point DoubleScalarMulBase(const Scalar& a, const Scalar& b, const Point& P);

/**
 * Ed25519 key pair
 */
struct KeyPair {
    Scalar secret;
    Point public_key;

    KeyPair() = default;
    KeyPair(const Scalar& s, const Point& p) : secret(s), public_key(p) {}

    // Generate random key pair
    static KeyPair Generate();

    // Derive from seed
    static KeyPair FromSeed(const uint8_t* seed);
    static KeyPair FromSeed(const std::array<uint8_t, SEED_SIZE>& seed);

    void Clear() {
        secret.Clear();
        public_key.Clear();
    }
};

/**
 * Ed25519 signature
 */
struct Signature {
    std::array<uint8_t, SIGNATURE_SIZE> data;

    Signature() { data.fill(0); }

    // Sign a message
    static Signature Sign(const KeyPair& keypair, const uint8_t* msg, size_t len);
    static Signature Sign(const KeyPair& keypair, const std::vector<uint8_t>& msg);

    // Verify a signature
    bool Verify(const Point& public_key, const uint8_t* msg, size_t len) const;
    bool Verify(const Point& public_key, const std::vector<uint8_t>& msg) const;

    std::vector<uint8_t> GetBytes() const;
};

} // namespace ed25519

#endif // WATTX_PRIVACY_ED25519_TYPES_H
