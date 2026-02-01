// Copyright (c) 2024 The WATTx developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_EQUIHASH_EQUIHASH_H
#define BITCOIN_CRYPTO_EQUIHASH_EQUIHASH_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>

/**
 * Equihash Proof-of-Work Implementation
 *
 * Equihash is a memory-hard proof-of-work algorithm based on the
 * generalized birthday problem. It was designed by Alex Biryukov
 * and Dmitry Khovratovich.
 *
 * This implementation supports Equihash<200,9> as used by ZCash.
 *
 * Parameters:
 *   n = 200 (collision bit length)
 *   k = 9   (number of rounds)
 *
 * Solution size: 2^k = 512 indices
 * Each index is (n/(k+1)+1) = 21 bits
 * Compressed solution: 1344 bytes (512 * 21 / 8)
 */

namespace equihash {

// Equihash<200,9> parameters (ZCash)
constexpr int N = 200;
constexpr int K = 9;

// Derived constants
constexpr int COLLISION_BIT_LENGTH = N / (K + 1);  // 20 bits
constexpr int COLLISION_BYTE_LENGTH = (COLLISION_BIT_LENGTH + 7) / 8;  // 3 bytes
constexpr int HASH_LENGTH = (K + 1) * COLLISION_BYTE_LENGTH;  // 30 bytes
constexpr int BLAKE2B_DIGEST_LENGTH = 50;  // N/4 bytes for Equihash
constexpr int INDICES_PER_HASH = 2;  // Number of indices per hash output
constexpr int NUM_INDICES = 1 << K;  // 512 indices
constexpr int INDEX_BIT_LENGTH = COLLISION_BIT_LENGTH + 1;  // 21 bits
constexpr int COMPRESSED_SOL_SIZE = NUM_INDICES * INDEX_BIT_LENGTH / 8;  // 1344 bytes

/**
 * Expand a compressed Equihash solution into indices
 *
 * @param compressed  Compressed solution (1344 bytes for 200,9)
 * @param indices     Output vector of 512 indices
 * @return true if expansion succeeded
 */
bool ExpandSolution(const std::vector<unsigned char>& compressed,
                    std::vector<uint32_t>& indices);

/**
 * Compress solution indices into compact form
 *
 * @param indices     Vector of 512 indices
 * @param compressed  Output compressed solution
 * @return true if compression succeeded
 */
bool CompressSolution(const std::vector<uint32_t>& indices,
                      std::vector<unsigned char>& compressed);

/**
 * Verify an Equihash solution
 *
 * @param header      Block header data (without solution)
 * @param headerLen   Length of header data
 * @param nonce       Nonce value
 * @param solution    Compressed solution
 * @return true if the solution is valid
 */
bool VerifySolution(const unsigned char* header, size_t headerLen,
                    uint32_t nonce,
                    const std::vector<unsigned char>& solution);

/**
 * Verify an Equihash solution (alternative interface)
 *
 * @param input       Combined header+nonce input
 * @param inputLen    Length of input
 * @param solution    Compressed solution
 * @return true if the solution is valid
 */
bool VerifySolution(const unsigned char* input, size_t inputLen,
                    const std::vector<unsigned char>& solution);

/**
 * Generate the initial hash values for given indices
 *
 * @param input       Input data (header + nonce)
 * @param inputLen    Input length
 * @param index       Index to generate hash for
 * @param hash        Output hash buffer (HASH_LENGTH bytes)
 */
void GenerateHash(const unsigned char* input, size_t inputLen,
                  uint32_t index, unsigned char* hash);

/**
 * Check if indices are in valid order (for solution verification)
 */
bool HasValidIndicesOrder(const std::vector<uint32_t>& indices);

/**
 * Get the expected solution size in bytes
 */
constexpr size_t GetSolutionSize() { return COMPRESSED_SOL_SIZE; }

/**
 * Validate solution format (size and basic structure)
 */
bool IsValidSolutionSize(size_t size);

} // namespace equihash

#endif // BITCOIN_CRYPTO_EQUIHASH_EQUIHASH_H
