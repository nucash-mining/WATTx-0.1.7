// Copyright (c) 2024 The WATTx developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/equihash/equihash.h>

// Use Blake2b from randomx
extern "C" {
#include <randomx/src/blake2/blake2.h>
}

#include <cstring>
#include <algorithm>
#include <set>

namespace equihash {

// Blake2b personalization for Equihash (ZCash uses "ZcashPoW")
static const unsigned char EQUIHASH_PERSONAL[16] = {
    'Z', 'c', 'a', 's', 'h', 'P', 'o', 'W',
    0xc8, 0x00, 0x00, 0x00,  // n = 200 (little-endian)
    0x09, 0x00, 0x00, 0x00   // k = 9 (little-endian)
};

// Initialize Blake2b state with Equihash parameters
static void InitializeState(blake2b_state* state, const unsigned char* input, size_t inputLen)
{
    blake2b_param P;
    memset(&P, 0, sizeof(P));
    P.digest_length = BLAKE2B_DIGEST_LENGTH;
    P.fanout = 1;
    P.depth = 1;
    memcpy(P.personal, EQUIHASH_PERSONAL, sizeof(EQUIHASH_PERSONAL));

    blake2b_init_param(state, &P);
    blake2b_update(state, input, inputLen);
}

void GenerateHash(const unsigned char* input, size_t inputLen,
                  uint32_t index, unsigned char* hash)
{
    blake2b_state state;
    InitializeState(&state, input, inputLen);

    // Add index (little-endian)
    unsigned char indexBytes[4];
    indexBytes[0] = index & 0xFF;
    indexBytes[1] = (index >> 8) & 0xFF;
    indexBytes[2] = (index >> 16) & 0xFF;
    indexBytes[3] = (index >> 24) & 0xFF;

    blake2b_state indexState = state;
    blake2b_update(&indexState, indexBytes, 4);

    unsigned char hashOutput[BLAKE2B_DIGEST_LENGTH];
    blake2b_final(&indexState, hashOutput, BLAKE2B_DIGEST_LENGTH);

    // Extract the relevant portion for this index
    // Each hash output produces multiple hash values
    memcpy(hash, hashOutput, HASH_LENGTH);
}

// Extract bits from a byte array
static uint32_t ExtractBits(const unsigned char* data, size_t bitOffset, size_t bitLength)
{
    uint32_t result = 0;
    size_t byteOffset = bitOffset / 8;
    size_t bitShift = bitOffset % 8;

    // Read enough bytes to cover the requested bits
    for (size_t i = 0; i < (bitLength + bitShift + 7) / 8 && i < 4; i++) {
        result |= ((uint32_t)data[byteOffset + i]) << (i * 8);
    }

    // Shift and mask to get the desired bits
    result >>= bitShift;
    result &= (1U << bitLength) - 1;

    return result;
}

// Pack bits into a byte array
static void PackBits(unsigned char* data, size_t bitOffset, size_t bitLength, uint32_t value)
{
    size_t byteOffset = bitOffset / 8;
    size_t bitShift = bitOffset % 8;

    value &= (1U << bitLength) - 1;  // Mask to bitLength bits

    // Pack the bits into the byte array
    for (size_t i = 0; i < (bitLength + bitShift + 7) / 8 && i < 4; i++) {
        data[byteOffset + i] |= (unsigned char)((value << bitShift) >> (i * 8));
    }
}

bool ExpandSolution(const std::vector<unsigned char>& compressed,
                    std::vector<uint32_t>& indices)
{
    if (compressed.size() != COMPRESSED_SOL_SIZE) {
        return false;
    }

    indices.resize(NUM_INDICES);

    for (int i = 0; i < NUM_INDICES; i++) {
        size_t bitOffset = i * INDEX_BIT_LENGTH;
        indices[i] = ExtractBits(compressed.data(), bitOffset, INDEX_BIT_LENGTH);
    }

    return true;
}

bool CompressSolution(const std::vector<uint32_t>& indices,
                      std::vector<unsigned char>& compressed)
{
    if (indices.size() != NUM_INDICES) {
        return false;
    }

    compressed.resize(COMPRESSED_SOL_SIZE, 0);

    for (int i = 0; i < NUM_INDICES; i++) {
        size_t bitOffset = i * INDEX_BIT_LENGTH;
        PackBits(compressed.data(), bitOffset, INDEX_BIT_LENGTH, indices[i]);
    }

    return true;
}

bool HasValidIndicesOrder(const std::vector<uint32_t>& indices)
{
    if (indices.size() != NUM_INDICES) {
        return false;
    }

    // Check for duplicates
    std::set<uint32_t> seen;
    for (uint32_t idx : indices) {
        if (seen.count(idx)) {
            return false;  // Duplicate index
        }
        seen.insert(idx);
    }

    // Check tree structure ordering
    // In a valid solution, for each level of the binary tree,
    // the left subtree's first index must be less than the right subtree's first index
    for (size_t step = 1; step < NUM_INDICES; step *= 2) {
        for (size_t i = 0; i < NUM_INDICES; i += step * 2) {
            if (indices[i] >= indices[i + step]) {
                return false;
            }
        }
    }

    return true;
}

// XOR two hash values
static void XorHashes(const unsigned char* a, const unsigned char* b,
                      unsigned char* result, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        result[i] = a[i] ^ b[i];
    }
}

// Check if hash is all zeros for the specified collision length
static bool IsValidCollision(const unsigned char* hash, size_t level)
{
    // At each level, check if the first (level+1)*COLLISION_BIT_LENGTH bits are zero
    size_t bitsToCheck = (level + 1) * COLLISION_BIT_LENGTH;
    size_t bytesToCheck = bitsToCheck / 8;
    size_t remainingBits = bitsToCheck % 8;

    for (size_t i = 0; i < bytesToCheck; i++) {
        if (hash[i] != 0) {
            return false;
        }
    }

    if (remainingBits > 0) {
        unsigned char mask = (1 << remainingBits) - 1;
        if ((hash[bytesToCheck] & mask) != 0) {
            return false;
        }
    }

    return true;
}

bool VerifySolution(const unsigned char* input, size_t inputLen,
                    const std::vector<unsigned char>& solution)
{
    // Check solution size
    if (solution.size() != COMPRESSED_SOL_SIZE) {
        return false;
    }

    // Expand the solution to indices
    std::vector<uint32_t> indices;
    if (!ExpandSolution(solution, indices)) {
        return false;
    }

    // Check indices ordering
    if (!HasValidIndicesOrder(indices)) {
        return false;
    }

    // Generate hashes for all indices
    std::vector<std::array<unsigned char, HASH_LENGTH>> hashes(NUM_INDICES);

    for (int i = 0; i < NUM_INDICES; i++) {
        GenerateHash(input, inputLen, indices[i], hashes[i].data());
    }

    // Verify the solution by computing XORs at each level
    for (int level = 0; level < K; level++) {
        size_t step = 1 << level;
        size_t collisionLen = COLLISION_BYTE_LENGTH * (K - level);

        for (size_t i = 0; i < NUM_INDICES; i += step * 2) {
            // XOR pairs of hashes
            std::array<unsigned char, HASH_LENGTH> xorResult;
            XorHashes(hashes[i].data(), hashes[i + step].data(),
                      xorResult.data(), collisionLen);

            // Check collision (first COLLISION_BIT_LENGTH bits should be zero)
            if (!IsValidCollision(xorResult.data(), 0)) {
                return false;
            }

            // Store result for next level (shift out the collision bits)
            if (level < K - 1) {
                memmove(hashes[i].data(), xorResult.data() + COLLISION_BYTE_LENGTH,
                        collisionLen - COLLISION_BYTE_LENGTH);
            } else {
                // Final level: entire hash should be zero
                for (size_t j = 0; j < collisionLen; j++) {
                    if (xorResult[j] != 0) {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

bool VerifySolution(const unsigned char* header, size_t headerLen,
                    uint32_t nonce,
                    const std::vector<unsigned char>& solution)
{
    // Combine header and nonce
    std::vector<unsigned char> input(headerLen + 4);
    memcpy(input.data(), header, headerLen);

    // Append nonce (little-endian)
    input[headerLen] = nonce & 0xFF;
    input[headerLen + 1] = (nonce >> 8) & 0xFF;
    input[headerLen + 2] = (nonce >> 16) & 0xFF;
    input[headerLen + 3] = (nonce >> 24) & 0xFF;

    return VerifySolution(input.data(), input.size(), solution);
}

bool IsValidSolutionSize(size_t size)
{
    return size == COMPRESSED_SOL_SIZE;
}

} // namespace equihash
