//! WATTx FCMP++ FFI Library
//!
//! This library provides C-compatible FFI functions for FCMP++ (Full-Chain Membership Proofs)
//! integration with WATTx. It wraps the Rust cryptographic operations and exposes them
//! through a stable C ABI.
//!
//! # Safety
//!
//! All FFI functions are marked `unsafe` and require valid pointers. The caller is responsible
//! for ensuring pointer validity and proper memory management.

use std::slice;
use std::ptr;

use rand_core::OsRng;
use zeroize::Zeroize;

// ============================================================================
// Error Codes
// ============================================================================

/// Success
pub const FCMP_SUCCESS: i32 = 0;
/// Invalid parameter (null pointer, wrong size, etc.)
pub const FCMP_ERROR_INVALID_PARAM: i32 = -1;
/// Proof generation failed
pub const FCMP_ERROR_PROOF_GENERATION: i32 = -2;
/// Proof verification failed
pub const FCMP_ERROR_PROOF_VERIFICATION: i32 = -3;
/// Memory allocation failed
pub const FCMP_ERROR_MEMORY: i32 = -4;
/// Invalid point on curve
pub const FCMP_ERROR_INVALID_POINT: i32 = -5;
/// Invalid scalar
pub const FCMP_ERROR_INVALID_SCALAR: i32 = -6;
/// Not initialized
pub const FCMP_ERROR_NOT_INITIALIZED: i32 = -7;
/// Internal error
pub const FCMP_ERROR_INTERNAL: i32 = -99;

// ============================================================================
// Constants
// ============================================================================

/// Size of a scalar in bytes
pub const SCALAR_SIZE: usize = 32;
/// Size of a point in bytes (compressed)
pub const POINT_SIZE: usize = 32;
/// Size of an output tuple (O, I, C = 3 points)
pub const OUTPUT_TUPLE_SIZE: usize = POINT_SIZE * 3;
/// Elements per output in field representation
pub const ELEMENTS_PER_OUTPUT: usize = 6;

// ============================================================================
// Opaque Types
// ============================================================================

/// Opaque handle to FCMP parameters
pub struct FcmpParams {
    // Generator points and precomputed tables
    _initialized: bool,
    // In full implementation, this would contain:
    // - Pedersen generators
    // - Hash initialization points
    // - Precomputed tables for fast MSM
}

/// Opaque handle to a proof
pub struct FcmpProof {
    data: Vec<u8>,
}

/// Branch data for proof generation
#[repr(C)]
pub struct FcmpBranch {
    /// Leaf index in the tree
    pub leaf_index: u64,
    /// Number of layers
    pub num_layers: u32,
    /// Pointer to layer data (array of FcmpBranchLayer)
    pub layers: *const FcmpBranchLayer,
}

/// Single layer of a branch
#[repr(C)]
pub struct FcmpBranchLayer {
    /// Number of elements in this layer
    pub num_elements: u32,
    /// Pointer to elements (array of 32-byte scalars)
    pub elements: *const u8,
}

/// Input tuple for verification
#[repr(C)]
pub struct FcmpInput {
    /// Re-randomized O point (x, y coordinates as scalars)
    pub o_tilde: [u8; 64],
    /// Re-randomized I point
    pub i_tilde: [u8; 64],
    /// R value for SA+L
    pub r: [u8; 64],
    /// Re-randomized C point
    pub c_tilde: [u8; 64],
}

// ============================================================================
// Global State
// ============================================================================

static mut GLOBAL_PARAMS: Option<Box<FcmpParams>> = None;

// ============================================================================
// Initialization Functions
// ============================================================================

/// Initialize the FCMP library with default parameters.
///
/// Must be called before any other FCMP functions.
/// Thread-safe for multiple calls (idempotent).
///
/// # Returns
/// - `FCMP_SUCCESS` on success
/// - `FCMP_ERROR_*` on failure
#[no_mangle]
pub unsafe extern "C" fn fcmp_init() -> i32 {
    if GLOBAL_PARAMS.is_some() {
        return FCMP_SUCCESS; // Already initialized
    }

    let params = Box::new(FcmpParams {
        _initialized: true,
    });

    GLOBAL_PARAMS = Some(params);
    FCMP_SUCCESS
}

/// Clean up and free FCMP resources.
///
/// After calling this, `fcmp_init()` must be called again before using other functions.
#[no_mangle]
pub unsafe extern "C" fn fcmp_cleanup() {
    GLOBAL_PARAMS = None;
}

/// Check if FCMP is initialized.
///
/// # Returns
/// - 1 if initialized
/// - 0 if not initialized
#[no_mangle]
pub unsafe extern "C" fn fcmp_is_initialized() -> i32 {
    if GLOBAL_PARAMS.is_some() { 1 } else { 0 }
}

// ============================================================================
// Scalar Operations
// ============================================================================

/// Generate a random scalar.
///
/// # Safety
/// - `out` must point to at least 32 bytes of writable memory
#[no_mangle]
pub unsafe extern "C" fn fcmp_scalar_random(out: *mut u8) -> i32 {
    if out.is_null() {
        return FCMP_ERROR_INVALID_PARAM;
    }

    use rand_core::RngCore;

    let mut scalar = [0u8; SCALAR_SIZE];
    if OsRng.try_fill_bytes(&mut scalar).is_err() {
        return FCMP_ERROR_INTERNAL;
    }

    // Reduce mod curve order (simplified - proper implementation uses curve25519-dalek)
    scalar[31] &= 0x7f;

    ptr::copy_nonoverlapping(scalar.as_ptr(), out, SCALAR_SIZE);
    scalar.zeroize();

    FCMP_SUCCESS
}

/// Add two scalars: out = a + b (mod l)
///
/// # Safety
/// - All pointers must point to at least 32 bytes
/// - `out` must be writable
#[no_mangle]
pub unsafe extern "C" fn fcmp_scalar_add(
    out: *mut u8,
    a: *const u8,
    b: *const u8,
) -> i32 {
    if out.is_null() || a.is_null() || b.is_null() {
        return FCMP_ERROR_INVALID_PARAM;
    }

    // Read inputs
    let a_bytes = slice::from_raw_parts(a, SCALAR_SIZE);
    let b_bytes = slice::from_raw_parts(b, SCALAR_SIZE);

    // Simple addition with carry (proper implementation would use curve25519-dalek)
    let mut result = [0u8; SCALAR_SIZE];
    let mut carry: u16 = 0;

    for i in 0..SCALAR_SIZE {
        let sum = a_bytes[i] as u16 + b_bytes[i] as u16 + carry;
        result[i] = sum as u8;
        carry = sum >> 8;
    }

    // Reduce mod l (simplified)
    result[31] &= 0x7f;

    ptr::copy_nonoverlapping(result.as_ptr(), out, SCALAR_SIZE);
    FCMP_SUCCESS
}

/// Multiply two scalars: out = a * b (mod l)
///
/// # Safety
/// - All pointers must point to at least 32 bytes
/// - `out` must be writable
#[no_mangle]
pub unsafe extern "C" fn fcmp_scalar_mul(
    out: *mut u8,
    a: *const u8,
    b: *const u8,
) -> i32 {
    if out.is_null() || a.is_null() || b.is_null() {
        return FCMP_ERROR_INVALID_PARAM;
    }

    // This is a placeholder - proper implementation would use curve25519-dalek
    // For now, just XOR the inputs (NOT cryptographically correct!)
    let a_bytes = slice::from_raw_parts(a, SCALAR_SIZE);
    let b_bytes = slice::from_raw_parts(b, SCALAR_SIZE);

    let mut result = [0u8; SCALAR_SIZE];
    for i in 0..SCALAR_SIZE {
        result[i] = a_bytes[i] ^ b_bytes[i];
    }
    result[31] &= 0x7f;

    ptr::copy_nonoverlapping(result.as_ptr(), out, SCALAR_SIZE);
    FCMP_SUCCESS
}

// ============================================================================
// Point Operations
// ============================================================================

/// Multiply a point by a scalar: out = scalar * point
///
/// # Safety
/// - All pointers must point to at least 32 bytes
/// - `out` must be writable
#[no_mangle]
pub unsafe extern "C" fn fcmp_point_mul(
    out: *mut u8,
    scalar: *const u8,
    point: *const u8,
) -> i32 {
    if out.is_null() || scalar.is_null() || point.is_null() {
        return FCMP_ERROR_INVALID_PARAM;
    }

    // Read inputs
    let scalar_bytes = slice::from_raw_parts(scalar, SCALAR_SIZE);
    let point_bytes = slice::from_raw_parts(point, POINT_SIZE);

    // Use curve25519-dalek for actual point multiplication
    use curve25519_dalek::edwards::CompressedEdwardsY;
    use curve25519_dalek::scalar::Scalar;

    let point_compressed = CompressedEdwardsY::from_slice(point_bytes);
    if point_compressed.is_err() {
        return FCMP_ERROR_INVALID_POINT;
    }
    let point_compressed = point_compressed.unwrap();

    let point_opt = point_compressed.decompress();
    if point_opt.is_none() {
        return FCMP_ERROR_INVALID_POINT;
    }
    let point = point_opt.unwrap();

    // Create scalar (clamp for Ed25519)
    let mut scalar_arr = [0u8; 32];
    scalar_arr.copy_from_slice(scalar_bytes);
    let scalar = Scalar::from_bytes_mod_order(scalar_arr);

    // Multiply
    let result = scalar * point;
    let result_bytes = result.compress().to_bytes();

    ptr::copy_nonoverlapping(result_bytes.as_ptr(), out, POINT_SIZE);
    FCMP_SUCCESS
}

/// Add two points: out = a + b
///
/// # Safety
/// - All pointers must point to at least 32 bytes
/// - `out` must be writable
#[no_mangle]
pub unsafe extern "C" fn fcmp_point_add(
    out: *mut u8,
    a: *const u8,
    b: *const u8,
) -> i32 {
    if out.is_null() || a.is_null() || b.is_null() {
        return FCMP_ERROR_INVALID_PARAM;
    }

    use curve25519_dalek::edwards::CompressedEdwardsY;

    let a_bytes = slice::from_raw_parts(a, POINT_SIZE);
    let b_bytes = slice::from_raw_parts(b, POINT_SIZE);

    let a_compressed = CompressedEdwardsY::from_slice(a_bytes);
    let b_compressed = CompressedEdwardsY::from_slice(b_bytes);

    if a_compressed.is_err() || b_compressed.is_err() {
        return FCMP_ERROR_INVALID_POINT;
    }

    let a_point = a_compressed.unwrap().decompress();
    let b_point = b_compressed.unwrap().decompress();

    if a_point.is_none() || b_point.is_none() {
        return FCMP_ERROR_INVALID_POINT;
    }

    let result = a_point.unwrap() + b_point.unwrap();
    let result_bytes = result.compress().to_bytes();

    ptr::copy_nonoverlapping(result_bytes.as_ptr(), out, POINT_SIZE);
    FCMP_SUCCESS
}

/// Get the Ed25519 base point (generator)
///
/// # Safety
/// - `out` must point to at least 32 bytes of writable memory
#[no_mangle]
pub unsafe extern "C" fn fcmp_point_basepoint(out: *mut u8) -> i32 {
    if out.is_null() {
        return FCMP_ERROR_INVALID_PARAM;
    }

    use curve25519_dalek::constants::ED25519_BASEPOINT_COMPRESSED;

    ptr::copy_nonoverlapping(
        ED25519_BASEPOINT_COMPRESSED.as_bytes().as_ptr(),
        out,
        POINT_SIZE,
    );

    FCMP_SUCCESS
}

/// Check if a point is valid (on the curve)
///
/// # Safety
/// - `point` must point to at least 32 bytes
///
/// # Returns
/// - 1 if valid
/// - 0 if invalid
#[no_mangle]
pub unsafe extern "C" fn fcmp_point_is_valid(point: *const u8) -> i32 {
    if point.is_null() {
        return 0;
    }

    use curve25519_dalek::edwards::CompressedEdwardsY;

    let point_bytes = slice::from_raw_parts(point, POINT_SIZE);
    let compressed = CompressedEdwardsY::from_slice(point_bytes);

    if compressed.is_err() {
        return 0;
    }

    if compressed.unwrap().decompress().is_some() { 1 } else { 0 }
}

// ============================================================================
// Hash Functions
// ============================================================================

/// Hash data to a scalar using BLAKE2b
///
/// # Safety
/// - `out` must point to at least 32 bytes of writable memory
/// - `data` must point to `data_len` bytes
#[no_mangle]
pub unsafe extern "C" fn fcmp_hash_to_scalar(
    out: *mut u8,
    data: *const u8,
    data_len: usize,
) -> i32 {
    if out.is_null() || (data.is_null() && data_len > 0) {
        return FCMP_ERROR_INVALID_PARAM;
    }

    use blake2::{Blake2b512, Digest};

    let input = if data_len > 0 {
        slice::from_raw_parts(data, data_len)
    } else {
        &[]
    };

    let mut hasher = Blake2b512::new();
    hasher.update(input);
    let hash = hasher.finalize();

    // Reduce to scalar (take first 32 bytes and reduce)
    let mut result = [0u8; SCALAR_SIZE];
    result.copy_from_slice(&hash[..SCALAR_SIZE]);
    result[31] &= 0x7f; // Simple reduction

    ptr::copy_nonoverlapping(result.as_ptr(), out, SCALAR_SIZE);
    FCMP_SUCCESS
}

/// Hash data to a point using BLAKE2b + Elligator-like mapping
///
/// # Safety
/// - `out` must point to at least 32 bytes of writable memory
/// - `data` must point to `data_len` bytes
#[no_mangle]
pub unsafe extern "C" fn fcmp_hash_to_point(
    out: *mut u8,
    data: *const u8,
    data_len: usize,
) -> i32 {
    if out.is_null() || (data.is_null() && data_len > 0) {
        return FCMP_ERROR_INVALID_PARAM;
    }

    use blake2::{Blake2b512, Digest};
    use curve25519_dalek::edwards::CompressedEdwardsY;

    let input = if data_len > 0 {
        slice::from_raw_parts(data, data_len)
    } else {
        &[]
    };

    // Hash to get uniform bytes
    let mut hasher = Blake2b512::new();
    hasher.update(b"WATTx_hash_to_point_v1");
    hasher.update(input);
    let hash = hasher.finalize();

    // Try to decode as point, increment and retry if invalid
    let mut attempt = [0u8; POINT_SIZE];
    for i in 0..=255u8 {
        let mut hasher2 = Blake2b512::new();
        hasher2.update(&hash);
        hasher2.update(&[i]);
        let h2 = hasher2.finalize();
        attempt.copy_from_slice(&h2[..POINT_SIZE]);

        let compressed = CompressedEdwardsY(attempt);
        if let Some(point) = compressed.decompress() {
            // Multiply by cofactor to ensure we're in the prime-order subgroup
            let result = point.mul_by_cofactor();
            let result_bytes = result.compress().to_bytes();
            ptr::copy_nonoverlapping(result_bytes.as_ptr(), out, POINT_SIZE);
            return FCMP_SUCCESS;
        }
    }

    FCMP_ERROR_INTERNAL
}

// ============================================================================
// Pedersen Commitment
// ============================================================================

/// Create a Pedersen commitment: C = value * G + blinding * H
///
/// # Safety
/// - `out` must point to at least 32 bytes of writable memory
/// - `value` and `blinding` must each point to 32 bytes
#[no_mangle]
pub unsafe extern "C" fn fcmp_pedersen_commit(
    out: *mut u8,
    value: *const u8,
    blinding: *const u8,
) -> i32 {
    if out.is_null() || value.is_null() || blinding.is_null() {
        return FCMP_ERROR_INVALID_PARAM;
    }

    use curve25519_dalek::constants::ED25519_BASEPOINT_POINT;
    use curve25519_dalek::scalar::Scalar;

    // Read scalars
    let value_bytes = slice::from_raw_parts(value, SCALAR_SIZE);
    let blinding_bytes = slice::from_raw_parts(blinding, SCALAR_SIZE);

    let mut v_arr = [0u8; 32];
    let mut b_arr = [0u8; 32];
    v_arr.copy_from_slice(value_bytes);
    b_arr.copy_from_slice(blinding_bytes);

    let v = Scalar::from_bytes_mod_order(v_arr);
    let b = Scalar::from_bytes_mod_order(b_arr);

    // G = base point, H = hash_to_point("WATTx_Pedersen_H")
    let g = ED25519_BASEPOINT_POINT;

    // Derive H deterministically
    let mut h_out = [0u8; POINT_SIZE];
    let h_seed = b"WATTx_Pedersen_H_v1";
    if fcmp_hash_to_point(h_out.as_mut_ptr(), h_seed.as_ptr(), h_seed.len()) != FCMP_SUCCESS {
        return FCMP_ERROR_INTERNAL;
    }

    use curve25519_dalek::edwards::CompressedEdwardsY;
    let h = CompressedEdwardsY(h_out).decompress().unwrap();

    // C = v*G + b*H
    let commitment = v * g + b * h;
    let result = commitment.compress().to_bytes();

    ptr::copy_nonoverlapping(result.as_ptr(), out, POINT_SIZE);
    FCMP_SUCCESS
}

// ============================================================================
// FCMP Proof Operations (Placeholder)
// ============================================================================

/// Estimate the proof size for given parameters
///
/// # Arguments
/// - `num_inputs` - Number of inputs being proven
/// - `num_layers` - Number of tree layers
///
/// # Returns
/// - Estimated proof size in bytes, or 0 on error
#[no_mangle]
pub unsafe extern "C" fn fcmp_proof_size(num_inputs: u32, num_layers: u32) -> usize {
    if num_inputs == 0 || num_layers == 0 {
        return 0;
    }

    // Simplified estimate based on FCMP++ paper:
    // Base: 16 elements for bulletproof components
    // + IPA rounds: 2 * log2(rows) per curve
    // + commitments
    // Roughly: 32 * (16 + 2*log2(n) + inputs*layers) + 64

    let base = 32 * 16;
    let ipa = 32 * 2 * (32 - (num_layers as u32).leading_zeros()) as usize;
    let commits = 32 * (num_inputs as usize) * (num_layers as usize);

    base + ipa + commits + 64
}

/// Generate an FCMP proof (placeholder implementation)
///
/// # Safety
/// - All pointers must be valid
/// - `proof_out` must have at least `proof_max_len` bytes available
/// - `proof_len_out` must be writable
///
/// # Returns
/// - `FCMP_SUCCESS` on success
/// - Error code on failure
#[no_mangle]
pub unsafe extern "C" fn fcmp_prove(
    proof_out: *mut u8,
    proof_len_out: *mut usize,
    proof_max_len: usize,
    tree_root: *const u8,
    output: *const u8,  // 96 bytes: O || I || C
    branch: *const FcmpBranch,
) -> i32 {
    if proof_out.is_null() || proof_len_out.is_null() ||
       tree_root.is_null() || output.is_null() || branch.is_null() {
        return FCMP_ERROR_INVALID_PARAM;
    }

    if GLOBAL_PARAMS.is_none() {
        return FCMP_ERROR_NOT_INITIALIZED;
    }

    // Read branch data
    let branch_ref = &*branch;
    if branch_ref.layers.is_null() || branch_ref.num_layers == 0 {
        return FCMP_ERROR_INVALID_PARAM;
    }

    // Placeholder proof: just hash all inputs together
    // Real implementation would use the full FCMP++ library

    use blake2::{Blake2b512, Digest};

    let mut hasher = Blake2b512::new();
    hasher.update(b"WATTx_FCMP_Proof_v1");
    hasher.update(slice::from_raw_parts(tree_root, POINT_SIZE));
    hasher.update(slice::from_raw_parts(output, OUTPUT_TUPLE_SIZE));

    // Include branch data
    let layers = slice::from_raw_parts(branch_ref.layers, branch_ref.num_layers as usize);
    for layer in layers {
        if !layer.elements.is_null() && layer.num_elements > 0 {
            let elements = slice::from_raw_parts(
                layer.elements,
                layer.num_elements as usize * SCALAR_SIZE
            );
            hasher.update(elements);
        }
    }

    let hash = hasher.finalize();

    // Create placeholder proof (64 bytes for now)
    let proof_len = 64;
    if proof_max_len < proof_len {
        return FCMP_ERROR_MEMORY;
    }

    ptr::copy_nonoverlapping(hash.as_ptr(), proof_out, proof_len);
    *proof_len_out = proof_len;

    FCMP_SUCCESS
}

/// Verify an FCMP proof (placeholder implementation)
///
/// # Safety
/// - All pointers must be valid
///
/// # Returns
/// - `FCMP_SUCCESS` if proof is valid
/// - `FCMP_ERROR_PROOF_VERIFICATION` if proof is invalid
/// - Other error codes on failure
#[no_mangle]
pub unsafe extern "C" fn fcmp_verify(
    tree_root: *const u8,
    input: *const FcmpInput,
    proof: *const u8,
    proof_len: usize,
) -> i32 {
    if tree_root.is_null() || input.is_null() || proof.is_null() {
        return FCMP_ERROR_INVALID_PARAM;
    }

    if GLOBAL_PARAMS.is_none() {
        return FCMP_ERROR_NOT_INITIALIZED;
    }

    if proof_len < 64 {
        return FCMP_ERROR_INVALID_PARAM;
    }

    // Placeholder verification: always succeeds if proof looks valid
    // Real implementation would use the full FCMP++ library

    let proof_bytes = slice::from_raw_parts(proof, proof_len);

    // Check proof isn't all zeros
    let all_zero = proof_bytes.iter().all(|&b| b == 0);
    if all_zero {
        return FCMP_ERROR_PROOF_VERIFICATION;
    }

    FCMP_SUCCESS
}

// ============================================================================
// Utility Functions
// ============================================================================

/// Get the library version string
///
/// # Returns
/// Pointer to a null-terminated version string
#[no_mangle]
pub extern "C" fn fcmp_version() -> *const i8 {
    b"0.1.0\0".as_ptr() as *const i8
}

/// Get error message for an error code
///
/// # Returns
/// Pointer to a null-terminated error string
#[no_mangle]
pub extern "C" fn fcmp_error_string(code: i32) -> *const i8 {
    match code {
        FCMP_SUCCESS => b"Success\0".as_ptr() as *const i8,
        FCMP_ERROR_INVALID_PARAM => b"Invalid parameter\0".as_ptr() as *const i8,
        FCMP_ERROR_PROOF_GENERATION => b"Proof generation failed\0".as_ptr() as *const i8,
        FCMP_ERROR_PROOF_VERIFICATION => b"Proof verification failed\0".as_ptr() as *const i8,
        FCMP_ERROR_MEMORY => b"Memory allocation failed\0".as_ptr() as *const i8,
        FCMP_ERROR_INVALID_POINT => b"Invalid curve point\0".as_ptr() as *const i8,
        FCMP_ERROR_INVALID_SCALAR => b"Invalid scalar\0".as_ptr() as *const i8,
        FCMP_ERROR_NOT_INITIALIZED => b"Library not initialized\0".as_ptr() as *const i8,
        FCMP_ERROR_INTERNAL => b"Internal error\0".as_ptr() as *const i8,
        _ => b"Unknown error\0".as_ptr() as *const i8,
    }
}

// ============================================================================
// Tests
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_init_cleanup() {
        unsafe {
            assert_eq!(fcmp_init(), FCMP_SUCCESS);
            assert_eq!(fcmp_is_initialized(), 1);
            fcmp_cleanup();
            assert_eq!(fcmp_is_initialized(), 0);
        }
    }

    #[test]
    fn test_point_operations() {
        unsafe {
            let mut basepoint = [0u8; POINT_SIZE];
            assert_eq!(fcmp_point_basepoint(basepoint.as_mut_ptr()), FCMP_SUCCESS);
            assert_eq!(fcmp_point_is_valid(basepoint.as_ptr()), 1);

            // Scalar mul: 2 * G
            let two = [2u8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
            let mut two_g = [0u8; POINT_SIZE];
            assert_eq!(fcmp_point_mul(two_g.as_mut_ptr(), two.as_ptr(), basepoint.as_ptr()), FCMP_SUCCESS);
            assert_eq!(fcmp_point_is_valid(two_g.as_ptr()), 1);

            // G + G should equal 2*G
            let mut g_plus_g = [0u8; POINT_SIZE];
            assert_eq!(fcmp_point_add(g_plus_g.as_mut_ptr(), basepoint.as_ptr(), basepoint.as_ptr()), FCMP_SUCCESS);
            assert_eq!(two_g, g_plus_g);
        }
    }

    #[test]
    fn test_hash_to_point() {
        unsafe {
            let data = b"test data";
            let mut point = [0u8; POINT_SIZE];
            assert_eq!(fcmp_hash_to_point(point.as_mut_ptr(), data.as_ptr(), data.len()), FCMP_SUCCESS);
            assert_eq!(fcmp_point_is_valid(point.as_ptr()), 1);

            // Same input should give same output
            let mut point2 = [0u8; POINT_SIZE];
            assert_eq!(fcmp_hash_to_point(point2.as_mut_ptr(), data.as_ptr(), data.len()), FCMP_SUCCESS);
            assert_eq!(point, point2);

            // Different input should give different output
            let data2 = b"other data";
            let mut point3 = [0u8; POINT_SIZE];
            assert_eq!(fcmp_hash_to_point(point3.as_mut_ptr(), data2.as_ptr(), data2.len()), FCMP_SUCCESS);
            assert_ne!(point, point3);
        }
    }

    #[test]
    fn test_pedersen_commit() {
        unsafe {
            let value = [42u8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
            let blinding = [1u8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

            let mut commitment = [0u8; POINT_SIZE];
            assert_eq!(fcmp_pedersen_commit(commitment.as_mut_ptr(), value.as_ptr(), blinding.as_ptr()), FCMP_SUCCESS);
            assert_eq!(fcmp_point_is_valid(commitment.as_ptr()), 1);

            // Same inputs should give same commitment
            let mut commitment2 = [0u8; POINT_SIZE];
            assert_eq!(fcmp_pedersen_commit(commitment2.as_mut_ptr(), value.as_ptr(), blinding.as_ptr()), FCMP_SUCCESS);
            assert_eq!(commitment, commitment2);
        }
    }
}
