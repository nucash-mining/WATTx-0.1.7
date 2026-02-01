// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef WATTX_MONERO_WALLET_H
#define WATTX_MONERO_WALLET_H

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace monero_wallet {

/**
 * Monero uses ed25519 keys (32 bytes each)
 */
using MoneroSecretKey = std::array<uint8_t, 32>;
using MoneroPublicKey = std::array<uint8_t, 32>;

/**
 * Monero address types
 */
enum class MoneroNetworkType {
    MAINNET = 0,
    TESTNET = 1,
    STAGENET = 2
};

/**
 * Monero account keys derived from WATTx seed
 */
struct MoneroAccountKeys {
    MoneroSecretKey spend_secret_key;
    MoneroPublicKey spend_public_key;
    MoneroSecretKey view_secret_key;
    MoneroPublicKey view_public_key;

    bool IsValid() const;
    void Clear();
};

/**
 * Monero address (95 characters for standard, 106 for integrated)
 */
struct MoneroAddress {
    std::string address;
    MoneroNetworkType network;
    bool is_subaddress;
    uint32_t major_index;  // Account
    uint32_t minor_index;  // Subaddress index

    bool IsValid() const;
    std::string GetShortAddress() const;  // First and last 6 chars
};

/**
 * Monero transaction output for balance tracking
 */
struct MoneroOutput {
    std::string tx_hash;
    uint64_t output_index;
    uint64_t amount;
    uint64_t block_height;
    bool spent;
    MoneroPublicKey output_public_key;
};

/**
 * Monero wallet balance info
 */
struct MoneroBalance {
    uint64_t balance;           // Total balance
    uint64_t unlocked_balance;  // Spendable balance
    uint64_t pending_balance;   // Pending incoming
    std::vector<MoneroOutput> outputs;
};

/**
 * Light Monero Wallet for WATTx
 *
 * This provides Monero wallet functionality integrated with WATTx:
 * - Derives Monero keys from WATTx wallet seed
 * - Generates Monero addresses for mining rewards
 * - Queries balance from Monero daemon (light wallet mode)
 * - Can create basic transactions (advanced features require full node)
 */
class MoneroLightWallet {
public:
    MoneroLightWallet();
    ~MoneroLightWallet();

    /**
     * Initialize wallet from WATTx seed
     * This derives Monero keys deterministically from the WATTx seed
     * @param seed 32-byte WATTx wallet seed
     * @param network Monero network (mainnet/testnet/stagenet)
     */
    bool InitFromSeed(const std::vector<uint8_t>& seed, MoneroNetworkType network = MoneroNetworkType::MAINNET);

    /**
     * Initialize wallet from existing Monero secret keys
     */
    bool InitFromKeys(const MoneroSecretKey& spend_key, const MoneroSecretKey& view_key,
                      MoneroNetworkType network = MoneroNetworkType::MAINNET);

    /**
     * Check if wallet is initialized
     */
    bool IsInitialized() const { return m_initialized; }

    /**
     * Get the primary address (account 0, address 0)
     */
    MoneroAddress GetPrimaryAddress() const;

    /**
     * Get a subaddress
     * @param account Account index (major)
     * @param index Address index within account (minor)
     */
    MoneroAddress GetSubaddress(uint32_t account, uint32_t index) const;

    /**
     * Get account keys (for advanced operations)
     */
    const MoneroAccountKeys& GetAccountKeys() const { return m_keys; }

    /**
     * Export view-only keys (for watch-only wallet)
     */
    std::string ExportViewKey() const;

    /**
     * Set Monero daemon connection for balance queries
     */
    void SetDaemonConnection(const std::string& host, uint16_t port);

    /**
     * Query balance from Monero daemon
     * Note: This is a simplified light wallet query, not full wallet sync
     */
    bool QueryBalance(MoneroBalance& balance);

    /**
     * Get mnemonic seed (25 words)
     */
    std::string GetMnemonicSeed() const;

    /**
     * Verify an address belongs to this wallet
     */
    bool IsOurAddress(const std::string& address) const;

    /**
     * Get the network type
     */
    MoneroNetworkType GetNetworkType() const { return m_network; }

private:
    // Derive public key from secret key using ed25519
    static MoneroPublicKey DerivePublicKey(const MoneroSecretKey& secret);

    // Derive view key from spend key (Monero's standard derivation)
    static MoneroSecretKey DeriveViewKey(const MoneroSecretKey& spend_key);

    // Generate subaddress keys
    std::pair<MoneroPublicKey, MoneroPublicKey> DeriveSubaddressKeys(
        uint32_t account, uint32_t index) const;

    // Encode address to base58
    static std::string EncodeAddress(const MoneroPublicKey& spend_pub,
                                      const MoneroPublicKey& view_pub,
                                      MoneroNetworkType network,
                                      bool is_subaddress);

    // Decode address from base58
    static bool DecodeAddress(const std::string& address,
                               MoneroPublicKey& spend_pub,
                               MoneroPublicKey& view_pub,
                               MoneroNetworkType& network,
                               bool& is_subaddress);

    // Keccak-256 hash (Monero's preferred hash)
    static std::array<uint8_t, 32> Keccak256(const void* data, size_t len);

    // Reduce scalar mod l (ed25519 curve order)
    static void ScalarReduce(MoneroSecretKey& key);

    // HTTP RPC call to daemon
    std::string DaemonRPC(const std::string& method, const std::string& params);

    MoneroAccountKeys m_keys;
    MoneroNetworkType m_network{MoneroNetworkType::MAINNET};
    bool m_initialized{false};

    std::string m_daemon_host;
    uint16_t m_daemon_port{18081};
};

/**
 * Base58 encoding for Monero addresses
 * Monero uses a modified base58 with 8-character blocks
 */
namespace base58 {
    std::string Encode(const std::vector<uint8_t>& data);
    bool Decode(const std::string& encoded, std::vector<uint8_t>& data);
    std::string EncodeCheck(const std::vector<uint8_t>& data);
    bool DecodeCheck(const std::string& encoded, std::vector<uint8_t>& data);
}

/**
 * Global Monero wallet instance accessor
 */
MoneroLightWallet& GetMoneroWallet();

/**
 * Network prefix bytes for address encoding
 */
constexpr uint8_t MAINNET_PUBLIC_ADDRESS_PREFIX = 18;
constexpr uint8_t MAINNET_PUBLIC_SUBADDRESS_PREFIX = 42;
constexpr uint8_t TESTNET_PUBLIC_ADDRESS_PREFIX = 53;
constexpr uint8_t TESTNET_PUBLIC_SUBADDRESS_PREFIX = 63;
constexpr uint8_t STAGENET_PUBLIC_ADDRESS_PREFIX = 24;
constexpr uint8_t STAGENET_PUBLIC_SUBADDRESS_PREFIX = 36;

}  // namespace monero_wallet

#endif  // WATTX_MONERO_WALLET_H
