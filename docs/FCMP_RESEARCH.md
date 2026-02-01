# FCMP++ (Full-Chain Membership Proofs) Research

## Overview

FCMP++ is Monero's next-generation privacy upgrade, replacing ring signatures with Full-Chain Membership Proofs. Instead of hiding among 11 decoys, transactions can prove membership in the **entire UTXO set**.

## Key Benefits

| Feature | Ring Signatures (Current) | FCMP++ (Fluorine Fermi) |
|---------|---------------------------|-------------------------|
| Anonymity Set | 11 decoys | Entire UTXO set |
| Statistical Analysis | Possible | Infeasible |
| Proof Size | ~2.5KB per input | Grows logarithmically |
| Verification Time | Fast | ~10-50ms |
| Generation Time | Fast | ~1 minute |

## Cryptographic Components

### 1. Curve Trees
A Merkle tree where the hash function is a Pedersen hash over elliptic curve points.

```
Root
├── Internal Node (Pedersen hash of children)
│   ├── Leaf (Output commitment)
│   └── Leaf (Output commitment)
└── Internal Node
    ├── Leaf (Output commitment)
    └── Leaf (Output commitment)
```

### 2. Helios/Selene Curve Cycle
- **Helios**: Primary curve
- **Selene**: Secondary curve (their order equals each other's field size)
- **Tower over Ed25519**: Allows reusing existing Monero UTXO set

Uses a **Crandall prime** enabling fast modular reduction.

### 3. Generalized Bulletproofs (GBP)
Modified Bulletproofs supporting Pedersen Vector Commitments for arithmetic circuit proofs.

### 4. Elliptic Curve Divisors
Technique for efficiently compressing large circuits, enabling practical proof sizes.

### 5. Spend Authorization + Linkability (SA+L)

**Key Image Generation** (unchanged concept):
```
I = x · H(K)
```
Where:
- `x` = private spend key
- `K` = one-time public key
- `I` = key image (prevents double-spend)

**GSP Matrix for SA+L**:
```
[G, T, 0]      → K'   (re-randomized pubkey)
[0, 0, V]      → B    (binding commitment)
[U, 0, 0]      → Z    (hint)
[I', 0, -Z]    → I    (key image)
```

## Implementation Architecture

### Monero's Approach (C++ + Rust)

**C++ handles:**
- Tree state management
- Database updates/reads
- Ed25519 math
- Transaction changes
- RPC routes

**Rust handles:**
- Helios/Selene curve math
- FCMP++ construction
- FCMP++ verification

### Key Repositories

1. **Main Implementation**: https://github.com/monero-project/monero/pull/9436
2. **FCMP++ Library**: migrated to monero-oxide `fcmp++` branch
3. **Helioselene**: https://github.com/kayabaNerve/fcmp-plus-plus/tree/develop/crypto/helioselene
4. **Specification**: https://gist.github.com/kayabaNerve/0e1f7719e5797c826b87249f21ab6f86

### Dependencies

- `crypto-bigint` - Big integer arithmetic
- `curve25519-dalek` - Ed25519 operations
- Custom Helios/Selene implementations

## WATTx Integration Plan

### Phase 1: Foundation (2-3 weeks)
1. Port helioselene curves to WATTx
2. Implement Crandall prime field arithmetic
3. Add tower cycle over secp256k1 (WATTx uses secp256k1, not Ed25519)

### Phase 2: Curve Trees (2-3 weeks)
1. Implement Pedersen hash for tree nodes
2. Build incremental Merkle tree structure
3. Integrate with COutputIndexDB

### Phase 3: Proofs (3-4 weeks)
1. Implement Generalized Bulletproofs
2. Add elliptic curve divisor library
3. Build FCMP circuit

### Phase 4: Integration (2-3 weeks)
1. Modify CPrivacyTransaction for FCMP
2. Update consensus validation
3. Add wallet support
4. RPC commands

### Challenges for WATTx

1. **Curve Choice**: WATTx uses secp256k1, Monero uses Ed25519. Need to:
   - Find/create a curve cycle towering secp256k1
   - Or migrate to Ed25519 for privacy transactions only

2. **UTXO Compatibility**: Need to track all P2PK outputs for tree

3. **Performance**: Proof generation is slow (~1 min). Consider:
   - Hardware acceleration
   - Precomputation caching
   - Parallel proof generation

## Alternative: Seraphis

Monero's original FCMP plan was tied to Seraphis (a new transaction protocol). FCMP++ was designed to avoid the migration Seraphis requires.

For WATTx, we could:
1. Use FCMP++ (no migration needed)
2. Use Seraphis (cleaner but requires address migration)
3. Hybrid approach (ring sigs for old UTXOs, FCMP for new)

## Security Considerations

- FCMP++ has undergone Veridise security audit (2025)
- No critical vulnerabilities found
- Relies on CDH (Computational Diffie-Hellman) assumption

## Performance Benchmarks (Monero testnet)

| Operation | Time |
|-----------|------|
| Proof generation (2 inputs) | ~60 seconds |
| Proof verification | ~30 ms |
| Proof size | ~1.5 KB base + logarithmic growth |

## Deep Technical Analysis (Code Study)

### Helioselene Curve Cycle Architecture

**Source:** `/home/nuts/Documents/WATTx/fcmp-research/fcmp-plus-plus/crypto/helioselene/`

The curve cycle consists of two short Weierstrass curves:
- **Helios**: Scalar field = HelioseleneField, Coordinate field = Field25519
- **Selene**: Scalar field = Field25519, Coordinate field = HelioseleneField

Each curve's scalar field is the other's coordinate field - this enables efficient Pedersen hashing while alternating between curves up the tree.

#### Crandall Prime (HelioseleneField)
```
Modulus: 0x7fffffffffffffffffffffffffffffffbf7f782cb7656b586eb6d2727927c79f
Form: p = 2^255 - k where k is small (Crandall prime)
```

Fast reduction: `(a mod 2^255) + k * (a >> 255) mod p`

#### Curve Equations
Both curves use: `y² = x³ - 3x + B` (short Weierstrass with a = -3)

```rust
// Helios (over Field25519)
B = 0x22e8c739b0ea70b8be94a76b3ebb7b3b043f6f384113bf3522b49ee1edd73ad4
G = (3, 0x537b74d97ac0721cbd92668350205f0759003bddc586a5dcd243e639e3183ef4)

// Selene (over HelioseleneField)
B = 0x70127713695876c17f51bba595ffe279f3944bdf06ae900e68de0983cb5a4558
G = (1, 0x7a19d927b85cca9257c93177455c825f938bb198c8f09b37741e0aa6a1d3fdd2)
```

### FCMP Proof Structure

**Source:** `/home/nuts/Documents/WATTx/fcmp-research/fcmp-plus-plus/crypto/fcmps/src/lib.rs`

#### FcmpCurves Trait (3 curves required)
```rust
pub trait FcmpCurves {
    type OC: Ciphersuite;  // Output curve (Ed25519 for Monero)
    type C1: Ciphersuite;  // First branch curve (Helios)
    type C2: Ciphersuite;  // Second branch curve (Selene)
}
```

#### Output Tuple (Leaf Element)
```rust
pub struct Output<G: Group> {
    O: G,  // One-time public key
    I: G,  // Key image
    C: G,  // Pedersen commitment (amount)
}
```

Each output expands to 6 field elements: `(O.x, O.y, I.x, I.y, C.x, C.y)`

#### Tree Layer Configuration
```rust
LAYER_ONE_LEN: 38    // C1 branch width
LAYER_TWO_LEN: 18    // C2 branch width
Leaves: 6 * 38 = 228 elements per commitment
```

#### Tree Structure
```
Layer 0 (Leaves): Ed25519 outputs → 228 elements → Pedersen hash → C1 point
Layer 1: C1 points → 38 elements → Pedersen hash → C2 point
Layer 2: C2 points → 18 elements → Pedersen hash → C1 point
Layer 3: C1 points → 38 elements → Pedersen hash → C2 point
... alternating up to root
```

### Circuit Architecture

**Source:** `/home/nuts/Documents/WATTx/fcmp-research/fcmp-plus-plus/crypto/fcmps/src/circuit.rs`

#### First Layer (SA+L Opening)
The first layer proves Spend Authorization + Linkability:

```rust
fn first_layer() {
    // 1. Prove O_tilde + o_blind*T = O
    // 2. Prove I_tilde + i_blind*U = I
    // 3. Prove R + i_blind*V = i_blind*blind (SA+L relation)
    // 4. Prove C_tilde + c_blind*G = C
    // 5. Prove (O, I, C) ∈ leaf_branch
}
```

#### Additional Layers (Branch Membership)
```rust
fn additional_layer() {
    // 1. Prove blinded_hash + blind*H = hash
    // 2. Prove hash.x ∈ branch_elements
}
```

### Tree Operations

**Source:** `/home/nuts/Documents/WATTx/fcmp-research/fcmp-plus-plus/crypto/fcmps/src/tree.rs`

```rust
// Pedersen hash: H_init + Σ(child_i * G_i)
fn hash_grow(generators, existing_hash, offset, existing_child, new_children) -> Point
fn hash_trim(generators, existing_hash, offset, children_to_remove, child_to_grow_back) -> Point
```

### Proof Size Analysis

```rust
fn proof_size(inputs: usize, layers: usize) -> usize {
    // Base: 16 elements (AI, AO, AS, tau_x, u, t_caret, a, b for each BP)
    // + IPA rounds: 2 * log2(rows) per curve
    // + Vector commitments
    // + t-polynomial commitments
    // Final: (32 * proof_elements) + 64 (root blind PoK)
}
```

For 2 inputs, 8 layers: ~1.5 KB

### Key Dependencies

| Crate | Purpose |
|-------|---------|
| `generalized-bulletproofs` | Arithmetic circuit proofs |
| `ec-divisors` | Efficient point arithmetic in circuits |
| `ciphersuite` | Curve abstraction layer |
| `multiexp` | Multi-exponentiation |
| `transcript` | Fiat-Shamir transcript |

### WATTx secp256k1 Challenge

The main challenge for WATTx is that it uses secp256k1, not Ed25519. Options:

1. **Find/Create secp256k1 Cycle**
   - Need curves C1, C2 where:
     - `|C1| = |secp256k1_field|`
     - `|C2| = |C1_field|`
     - `|C1_field| = |C2|`
   - Research needed on secq256k1 or similar

2. **Use Ed25519 for Privacy Layer Only**
   - Privacy outputs use Ed25519 keys
   - Bridge from secp256k1 addresses via stealth address derivation
   - Simplest approach - can reuse Monero's implementation directly

3. **Hybrid Approach**
   - Normal transactions: secp256k1
   - Privacy shielding: Ed25519 on-ramp
   - All privacy operations in Ed25519 land

**Recommendation:** Option 2 (Ed25519 for privacy) is most practical. The helioselene curves are specifically designed for Ed25519 and have been security audited.

## References

1. [FCMP++ Specification](https://gist.github.com/kayabaNerve/0e1f7719e5797c826b87249f21ab6f86)
2. [Monero FCMP++ PR](https://github.com/monero-project/monero/pull/9436)
3. [Curve Trees Paper](https://eprint.iacr.org/2022/756)
4. [Veridise Security Audit](https://www.getmonero.org/2025/04/05/fcmp++-contest.html)
5. [FCMP++ Optimization Competition](https://github.com/j-berman/fcmp-plus-plus-optimization-competition)

---
*Last updated: 2026-01-29*
