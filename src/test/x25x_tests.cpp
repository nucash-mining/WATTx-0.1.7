// Copyright (c) 2024 The WATTx developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <crypto/x25x/x25x.h>
#include <primitives/block.h>
#include <uint256.h>
#include <streams.h>

#include <cstring>

BOOST_AUTO_TEST_SUITE(x25x_tests)

// Test data - a simple block header for testing
static CBlockHeader CreateTestHeader()
{
    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ONE;
    header.nTime = 1700000000;
    header.nBits = 0x1d00ffff;
    header.nNonce = 12345;
    return header;
}

BOOST_AUTO_TEST_CASE(algorithm_info_test)
{
    // Test that all algorithms have proper info
    auto algos = x25x::GetEnabledAlgorithms();
    BOOST_CHECK(algos.size() >= 6);  // At least 6 algorithms should be enabled

    for (auto algo : algos) {
        const auto& info = x25x::GetAlgorithmInfo(algo);
        BOOST_CHECK(!info.name.empty());
        BOOST_CHECK(!info.description.empty());
        BOOST_CHECK(info.enabled);
    }
}

BOOST_AUTO_TEST_CASE(algorithm_by_name_test)
{
    // Test algorithm lookup by name
    BOOST_CHECK(x25x::GetAlgorithmByName("sha256d") == x25x::Algorithm::SHA256D);
    BOOST_CHECK(x25x::GetAlgorithmByName("SHA256D") == x25x::Algorithm::SHA256D);
    BOOST_CHECK(x25x::GetAlgorithmByName("scrypt") == x25x::Algorithm::SCRYPT);
    BOOST_CHECK(x25x::GetAlgorithmByName("ethash") == x25x::Algorithm::ETHASH);
    BOOST_CHECK(x25x::GetAlgorithmByName("randomx") == x25x::Algorithm::RANDOMX);
    BOOST_CHECK(x25x::GetAlgorithmByName("equihash") == x25x::Algorithm::EQUIHASH);
    BOOST_CHECK(x25x::GetAlgorithmByName("x11") == x25x::Algorithm::X11);
    BOOST_CHECK(x25x::GetAlgorithmByName("kheavyhash") == x25x::Algorithm::KHEAVYHASH);

    // Test aliases
    BOOST_CHECK(x25x::GetAlgorithmByName("litecoin") == x25x::Algorithm::SCRYPT);
    BOOST_CHECK(x25x::GetAlgorithmByName("monero") == x25x::Algorithm::RANDOMX);
    BOOST_CHECK(x25x::GetAlgorithmByName("zcash") == x25x::Algorithm::EQUIHASH);
    BOOST_CHECK(x25x::GetAlgorithmByName("dash") == x25x::Algorithm::X11);
    BOOST_CHECK(x25x::GetAlgorithmByName("kaspa") == x25x::Algorithm::KHEAVYHASH);
}

BOOST_AUTO_TEST_CASE(block_version_algorithm_encoding)
{
    // Test encoding/decoding algorithm in block version
    int32_t version = 0x20000000;  // BIP9 version bits

    // Test each algorithm
    for (int i = 0; i <= 7; i++) {
        x25x::Algorithm algo = static_cast<x25x::Algorithm>(i);
        int32_t encodedVersion = x25x::SetBlockAlgorithm(version, algo);
        x25x::Algorithm decoded = x25x::GetBlockAlgorithm(encodedVersion);
        BOOST_CHECK(decoded == algo);
    }

    // Test that low bits are preserved
    version = 0x20000001;  // version with low bit set
    int32_t encoded = x25x::SetBlockAlgorithm(version, x25x::Algorithm::SCRYPT);
    BOOST_CHECK((encoded & 0xFF) == 0x01);  // Low byte preserved
    BOOST_CHECK(x25x::GetBlockAlgorithm(encoded) == x25x::Algorithm::SCRYPT);
}

BOOST_AUTO_TEST_CASE(sha256d_hash_test)
{
    CBlockHeader header = CreateTestHeader();
    header.nVersion = x25x::SetBlockAlgorithm(header.nVersion, x25x::Algorithm::SHA256D);

    uint256 hash = x25x::HashBlockHeader(header, x25x::Algorithm::SHA256D);

    // Hash should not be zero
    BOOST_CHECK(!hash.IsNull());

    // Same input should produce same hash
    uint256 hash2 = x25x::HashBlockHeader(header, x25x::Algorithm::SHA256D);
    BOOST_CHECK(hash == hash2);

    // Different nonce should produce different hash
    header.nNonce = 54321;
    uint256 hash3 = x25x::HashBlockHeader(header, x25x::Algorithm::SHA256D);
    BOOST_CHECK(hash != hash3);
}

BOOST_AUTO_TEST_CASE(scrypt_hash_test)
{
    CBlockHeader header = CreateTestHeader();
    header.nVersion = x25x::SetBlockAlgorithm(header.nVersion, x25x::Algorithm::SCRYPT);

    uint256 hash = x25x::HashBlockHeader(header, x25x::Algorithm::SCRYPT);

    // Hash should not be zero
    BOOST_CHECK(!hash.IsNull());

    // Same input should produce same hash
    uint256 hash2 = x25x::HashBlockHeader(header, x25x::Algorithm::SCRYPT);
    BOOST_CHECK(hash == hash2);

    // Should be different from SHA256D
    uint256 sha256hash = x25x::HashBlockHeader(header, x25x::Algorithm::SHA256D);
    BOOST_CHECK(hash != sha256hash);
}

BOOST_AUTO_TEST_CASE(ethash_hash_test)
{
    CBlockHeader header = CreateTestHeader();
    header.nVersion = x25x::SetBlockAlgorithm(header.nVersion, x25x::Algorithm::ETHASH);

    // Ethash requires block height for epoch calculation
    uint256 hash = x25x::HashBlockHeader(header, x25x::Algorithm::ETHASH, 1000);

    // Hash should not be zero
    BOOST_CHECK(!hash.IsNull());

    // Same input should produce same hash
    uint256 hash2 = x25x::HashBlockHeader(header, x25x::Algorithm::ETHASH, 1000);
    BOOST_CHECK(hash == hash2);
}

BOOST_AUTO_TEST_CASE(randomx_hash_test)
{
    CBlockHeader header = CreateTestHeader();
    header.nVersion = x25x::SetBlockAlgorithm(header.nVersion, x25x::Algorithm::RANDOMX);

    uint256 hash = x25x::HashBlockHeader(header, x25x::Algorithm::RANDOMX);

    // Hash should not be zero (RandomX should be initialized)
    BOOST_CHECK(!hash.IsNull());

    // Same input should produce same hash
    uint256 hash2 = x25x::HashBlockHeader(header, x25x::Algorithm::RANDOMX);
    BOOST_CHECK(hash == hash2);
}

BOOST_AUTO_TEST_CASE(x11_hash_test)
{
    CBlockHeader header = CreateTestHeader();
    header.nVersion = x25x::SetBlockAlgorithm(header.nVersion, x25x::Algorithm::X11);

    uint256 hash = x25x::HashBlockHeader(header, x25x::Algorithm::X11);

    // Hash should not be zero
    BOOST_CHECK(!hash.IsNull());

    // Same input should produce same hash
    uint256 hash2 = x25x::HashBlockHeader(header, x25x::Algorithm::X11);
    BOOST_CHECK(hash == hash2);

    // Should be different from other algorithms
    uint256 sha256hash = x25x::HashBlockHeader(header, x25x::Algorithm::SHA256D);
    BOOST_CHECK(hash != sha256hash);
}

BOOST_AUTO_TEST_CASE(kheavyhash_hash_test)
{
    CBlockHeader header = CreateTestHeader();
    header.nVersion = x25x::SetBlockAlgorithm(header.nVersion, x25x::Algorithm::KHEAVYHASH);

    uint256 hash = x25x::HashBlockHeader(header, x25x::Algorithm::KHEAVYHASH);

    // Hash should not be zero
    BOOST_CHECK(!hash.IsNull());

    // Same input should produce same hash
    uint256 hash2 = x25x::HashBlockHeader(header, x25x::Algorithm::KHEAVYHASH);
    BOOST_CHECK(hash == hash2);
}

BOOST_AUTO_TEST_CASE(all_algorithms_different_output)
{
    // Test that all algorithms produce different hashes for the same input
    CBlockHeader header = CreateTestHeader();

    std::vector<x25x::Algorithm> algos = {
        x25x::Algorithm::SHA256D,
        x25x::Algorithm::SCRYPT,
        x25x::Algorithm::X11,
        x25x::Algorithm::KHEAVYHASH
        // Note: Ethash and RandomX require special initialization
    };

    std::vector<uint256> hashes;
    for (auto algo : algos) {
        header.nVersion = x25x::SetBlockAlgorithm(header.nVersion, algo);
        uint256 hash = x25x::HashBlockHeader(header, algo);
        BOOST_CHECK(!hash.IsNull());
        hashes.push_back(hash);
    }

    // All hashes should be unique
    for (size_t i = 0; i < hashes.size(); i++) {
        for (size_t j = i + 1; j < hashes.size(); j++) {
            BOOST_CHECK(hashes[i] != hashes[j]);
        }
    }
}

BOOST_AUTO_TEST_CASE(hash_raw_data_test)
{
    // Test hashing raw data directly
    const unsigned char testData[] = "WATTx X25X Multi-Algorithm Test";
    size_t dataLen = sizeof(testData) - 1;

    // SHA256D
    uint256 sha256d = x25x::hash::SHA256D(testData, dataLen);
    BOOST_CHECK(!sha256d.IsNull());

    // Scrypt
    uint256 scrypt = x25x::hash::Scrypt(testData, dataLen);
    BOOST_CHECK(!scrypt.IsNull());

    // X11
    uint256 x11 = x25x::hash::X11(testData, dataLen);
    BOOST_CHECK(!x11.IsNull());

    // kHeavyHash
    uint256 kheavy = x25x::hash::KHeavyHash(testData, dataLen);
    BOOST_CHECK(!kheavy.IsNull());

    // All should be different
    BOOST_CHECK(sha256d != scrypt);
    BOOST_CHECK(sha256d != x11);
    BOOST_CHECK(sha256d != kheavy);
    BOOST_CHECK(scrypt != x11);
    BOOST_CHECK(scrypt != kheavy);
    BOOST_CHECK(x11 != kheavy);
}

BOOST_AUTO_TEST_SUITE_END()
