# FCMP++ Integration Plan for WATTx

## Research Status: COMPLETE

The FCMP++ codebase has been studied and verified:
- **Helioselene curves**: 10/10 tests pass
- **Generalized Bulletproofs**: 5/5 tests pass
- **Full FCMP library**: Compiles successfully

## Key Architecture Decision: Use Ed25519 for Privacy Layer

**Recommendation:** Use Ed25519 for all privacy-related operations rather than trying to create a secp256k1 curve cycle.

### Rationale:
1. Helioselene curves are specifically designed and audited for Ed25519
2. No known secure curve cycle exists for secp256k1
3. Creating a new curve cycle requires extensive cryptographic review
4. Ed25519 is proven secure and widely used (Monero, Zcash, etc.)

### Privacy Layer Design:
```
+------------------+     +-------------------+     +------------------+
|  WATTx Native    |     |   Privacy Layer   |     |  FCMP Proof      |
|  (secp256k1)     | --> |   (Ed25519)       | --> |  (Helios/Selene) |
+------------------+     +-------------------+     +------------------+
        |                        |                        |
   Regular TX             Stealth Addresses          Full-Chain
   P2PKH/P2SH            Ring Signatures (v1)      Membership Proof
                         Key Images
```

## Implementation Phases

### Phase 1: Ed25519 Integration (Foundation)
**Dependencies to add:**
- `curve25519-dalek` - Ed25519 operations
- `helioselene` - Curve cycle (port from Rust or FFI)
- `crypto-bigint` - Big integer arithmetic

**C++ Implementation:**
```cpp
// src/privacy/ed25519/
- ed25519_types.h      // Ed25519 point and scalar types
- ed25519_ops.cpp      // Basic operations (add, mul, hash_to_curve)
- pedersen.h           // Pedersen commitments over Ed25519
```

**Tasks:**
1. Add Ed25519 library dependency
2. Implement Ed25519 primitives in C++
3. Create conversion utilities between secp256k1 addresses and Ed25519 stealth addresses
4. Unit tests for all primitives

### Phase 2: Curve Tree Implementation
**New files:**
```cpp
// src/privacy/curvetree/
- curve_tree.h         // Tree structure and operations
- pedersen_hash.h      // Pedersen hash for tree nodes
- tree_db.h            // LevelDB storage for tree state
```

**Tasks:**
1. Implement Pedersen vector commitment
2. Build incremental Merkle tree structure
3. Create persistent storage layer (LevelDB)
4. Tree update operations (add output, rebuild)
5. Branch extraction for proofs

### Phase 3: Rust FFI for FCMP Proofs
**Approach:** Use Rust for proof generation/verification via FFI

**Directory structure:**
```
src/privacy/fcmp/
├── rust/
│   ├── Cargo.toml
│   └── src/
│       └── lib.rs        // FFI exports
├── fcmp_ffi.h            // C++ FFI header
└── fcmp_ffi.cpp          // C++ FFI wrapper
```

**FFI Interface:**
```rust
#[no_mangle]
pub extern "C" fn fcmp_prove(
    params: *const FcmpParams,
    branches: *const BranchData,
    output: *mut ProofBuffer,
) -> i32;

#[no_mangle]
pub extern "C" fn fcmp_verify(
    params: *const FcmpParams,
    tree_root: *const u8,
    inputs: *const InputData,
    proof: *const u8,
    proof_len: usize,
) -> i32;
```

### Phase 4: Transaction Integration
**Modified files:**
```cpp
// Updated privacy transaction structure
struct CPrivacyTransactionV2 {
    // Existing fields
    std::vector<CPrivacyInput> privacyInputs;
    std::vector<CPrivacyOutput> privacyOutputs;

    // FCMP-specific
    std::vector<uint8_t> fcmpProof;       // The FCMP proof
    uint8_t treeRootType;                  // C1 or C2 root
    std::array<uint8_t, 32> treeRoot;      // Tree root commitment
    std::vector<Input> fcmpInputs;         // Re-randomized input tuples
};
```

**Consensus changes:**
1. New transaction version for FCMP (v3)
2. Validate FCMP proofs instead of ring signatures
3. Track tree root in block headers (optional)
4. Key image validation (unchanged)

### Phase 5: Wallet Integration
**New wallet features:**
1. Ed25519 stealth address generation
2. Output scanning (same as current)
3. Branch extraction for proof generation
4. Proof caching for faster spending

## File Structure Summary

```
src/privacy/
├── ed25519/                    # Ed25519 primitives
│   ├── ed25519_types.h
│   ├── ed25519_ops.h
│   └── pedersen.h
├── curvetree/                  # Curve tree implementation
│   ├── curve_tree.h
│   ├── pedersen_hash.h
│   └── tree_db.h
├── fcmp/                       # FCMP proofs (FFI to Rust)
│   ├── rust/                   # Rust library
│   ├── fcmp_ffi.h
│   └── fcmp_ffi.cpp
├── privacy.h                   # Updated with FCMP types
├── privacy.cpp
├── consensus.h                 # Updated validation
└── consensus.cpp
```

## Testing Strategy

1. **Unit tests:** Ed25519 ops, Pedersen hash, tree operations
2. **Integration tests:** Proof generation/verification round-trip
3. **Functional tests:** Full transaction flow (shield → spend)
4. **Performance tests:** Proof generation time, verification time
5. **Consensus tests:** Reorg handling, tree state consistency

## Migration Path

### Option A: Hard Fork (Clean)
- New transaction version (v3) at activation height
- Old ring signature transactions (v1) remain valid
- Tree built from all outputs at activation
- ~1 week sync time for tree construction

### Option B: Soft Fork (Compatible)
- FCMP transactions as extension blocks
- Existing chain unmodified
- Privacy layer as opt-in overlay
- Simpler but less integrated

**Recommendation:** Option A (hard fork) for cleaner integration

## Performance Expectations

| Metric | Ring Signatures (Current) | FCMP++ |
|--------|---------------------------|--------|
| Proof Generation | ~100ms | ~60s per input |
| Proof Verification | ~5ms | ~30ms |
| Proof Size | ~2.5KB/input | ~1.5KB base + log(n) |
| Anonymity Set | 11 | Entire UTXO set |

## Dependencies

### Rust (for FCMP proofs)
```toml
[dependencies]
full-chain-membership-proofs = { path = "..." }
helioselene = { path = "..." }
```

### C++ (existing + new)
- LevelDB (existing)
- Boost (existing)
- OpenSSL (existing) - may need Ed25519 from separate library

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| FFI complexity | Medium | Medium | Extensive testing, memory safety |
| Proof generation time | High | Low | User education, async generation |
| Tree sync time | Medium | Medium | Checkpointing, fast sync |
| Security audit needed | High | High | Budget for professional audit |

## Timeline Estimate

| Phase | Duration | Dependencies |
|-------|----------|--------------|
| Phase 1: Ed25519 | 2-3 weeks | None |
| Phase 2: Curve Tree | 2-3 weeks | Phase 1 |
| Phase 3: Rust FFI | 3-4 weeks | Phase 2 |
| Phase 4: TX Integration | 2-3 weeks | Phase 3 |
| Phase 5: Wallet | 2-3 weeks | Phase 4 |
| Testing & Audit | 4-6 weeks | All phases |

**Total:** ~4-5 months

## Next Steps

1. [ ] Set up Rust build integration with CMake
2. [ ] Port or wrap helioselene for C++ usage
3. [ ] Implement Ed25519 primitives
4. [ ] Create tree storage schema
5. [ ] Build FFI layer for proof generation

---
*Created: 2026-01-29*
