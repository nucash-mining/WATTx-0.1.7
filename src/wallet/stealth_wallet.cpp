// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/stealth_wallet.h>
#include <wallet/wallet.h>
#include <hash.h>
#include <logging.h>
#include <script/script.h>
#include <script/solver.h>
#include <key_io.h>
#include <random.h>
#include <streams.h>
#include <util/fs.h>

#include <fstream>

namespace wallet {

CStealthAddressManager::CStealthAddressManager(CWallet* wallet)
    : m_wallet(wallet)
{
}

CStealthAddressManager::~CStealthAddressManager() = default;

bool CStealthAddressManager::GenerateStealthAddress(const std::string& label,
                                                      CStealthAddressData& addressData)
{
    LOCK(cs_stealth);

    // Generate new random keys
    CKey scanKey, spendKey;
    scanKey.MakeNewKey(true);
    spendKey.MakeNewKey(true);

    // Create stealth address
    addressData.address = privacy::CStealthAddress(scanKey.GetPubKey(), spendKey.GetPubKey());
    addressData.scanPrivKey = scanKey;
    addressData.spendPrivKey = spendKey;
    addressData.label = label;
    addressData.nCreateTime = GetTime();

    if (!addressData.IsValid()) {
        return false;
    }

    // Store in memory
    uint256 addrHash = HashStealthAddress(addressData.address);
    m_stealthAddresses[addrHash] = addressData;

    LogPrintf("Generated new stealth address: %s (label: %s)\n",
              addressData.address.ToString(), label);
    return true;
}

bool CStealthAddressManager::ImportStealthAddress(const CKey& scanKey, const CKey& spendKey,
                                                    const std::string& label,
                                                    CStealthAddressData& addressData)
{
    LOCK(cs_stealth);

    // Create stealth address from keys
    addressData.address = privacy::CStealthAddress(scanKey.GetPubKey(), spendKey.GetPubKey());
    addressData.scanPrivKey = scanKey;
    addressData.spendPrivKey = spendKey;
    addressData.label = label;
    addressData.nCreateTime = GetTime();

    if (!addressData.IsValid()) {
        return false;
    }

    // Check if already exists
    uint256 addrHash = HashStealthAddress(addressData.address);
    if (m_stealthAddresses.count(addrHash)) {
        LogPrintf("Stealth address already exists: %s\n", addressData.address.ToString());
        return false;
    }

    m_stealthAddresses[addrHash] = addressData;

    LogPrintf("Imported stealth address: %s (label: %s)\n",
              addressData.address.ToString(), label);
    return true;
}

std::vector<CStealthAddressData> CStealthAddressManager::GetStealthAddresses() const
{
    LOCK(cs_stealth);
    std::vector<CStealthAddressData> result;
    result.reserve(m_stealthAddresses.size());
    for (const auto& [hash, data] : m_stealthAddresses) {
        result.push_back(data);
    }
    return result;
}

std::optional<CStealthAddressData> CStealthAddressManager::GetStealthAddressByLabel(
    const std::string& label) const
{
    LOCK(cs_stealth);
    for (const auto& [hash, data] : m_stealthAddresses) {
        if (data.label == label) {
            return data;
        }
    }
    return std::nullopt;
}

std::optional<CStealthAddressData> CStealthAddressManager::GetStealthAddressByHash(
    const uint256& hash) const
{
    LOCK(cs_stealth);
    auto it = m_stealthAddresses.find(hash);
    if (it != m_stealthAddresses.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<CStealthPayment> CStealthAddressManager::TryDetectPayment(
    const CTxOut& txout,
    uint32_t outputIndex,
    const uint256& txid,
    int blockHeight)
{
    // Extract pubkey from output
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(txout.scriptPubKey, solutions);

    CPubKey outputPubKey;
    if (type == TxoutType::PUBKEY && solutions.size() >= 1) {
        outputPubKey = CPubKey(solutions[0]);
    } else {
        // Not a P2PK output, can't be stealth
        return std::nullopt;
    }

    if (!outputPubKey.IsFullyValid()) {
        return std::nullopt;
    }

    // Look for ephemeral pubkey in OP_RETURN output (would need context of full tx)
    // For now, this is a simplified detection that tries each address
    // In practice, ephemeral pubkey would be encoded in a preceding OP_RETURN output

    // Try each of our stealth addresses
    for (const auto& [addrHash, addrData] : m_stealthAddresses) {
        // In a full implementation, we would:
        // 1. Find the ephemeral pubkey R from OP_RETURN
        // 2. Compute shared secret S = scan_privkey * R
        // 3. Derive expected pubkey P = spend_pubkey + H(S)*G
        // 4. Check if P matches outputPubKey

        // For now, use the basic detection with ephemeral pubkey assumed from context
        // This is a placeholder - real implementation needs full tx context

        CKey derivedKey;
        // Try with output pubkey as ephemeral (simplified - not correct protocol)
        if (privacy::DeriveStealthSpendingKey(addrData.scanPrivKey, addrData.spendPrivKey,
                                               outputPubKey, outputIndex, derivedKey)) {
            // Verify the derived public key matches
            if (derivedKey.GetPubKey() == outputPubKey) {
                CStealthPayment payment;
                payment.txid = txid;
                payment.nOutput = outputIndex;
                payment.nValue = txout.nValue;
                payment.oneTimePubKey = outputPubKey;
                payment.derivedPrivKey = derivedKey;
                payment.stealthAddressHash = addrHash;
                payment.blockHeight = blockHeight;
                payment.spent = false;

                LogPrintf("Detected stealth payment: %s:%d, amount=%d, to address=%s\n",
                          txid.ToString(), outputIndex, txout.nValue, addrData.address.ToString());
                return payment;
            }
        }
    }

    return std::nullopt;
}

std::vector<CStealthPayment> CStealthAddressManager::ScanTransactionForPayments(
    const CTransaction& tx)
{
    LOCK(cs_stealth);
    std::vector<CStealthPayment> payments;

    if (m_stealthAddresses.empty()) {
        return payments;
    }

    uint256 txid = tx.GetHash();

    for (uint32_t i = 0; i < tx.vout.size(); i++) {
        auto payment = TryDetectPayment(tx.vout[i], i, txid, -1);
        if (payment) {
            COutPoint outpoint(Txid::FromUint256(txid), i);

            // Check if we already have this payment
            if (m_payments.find(outpoint) == m_payments.end()) {
                m_payments[outpoint] = *payment;
                m_paymentKeys[outpoint] = payment->derivedPrivKey;
            }

            payments.push_back(*payment);
        }
    }

    return payments;
}

std::vector<CStealthPayment> CStealthAddressManager::ScanBlockForPayments(
    const CBlock& block, int height)
{
    LOCK(cs_stealth);
    std::vector<CStealthPayment> payments;

    if (m_stealthAddresses.empty()) {
        return payments;
    }

    for (const auto& tx : block.vtx) {
        uint256 txid = tx->GetHash();

        for (uint32_t i = 0; i < tx->vout.size(); i++) {
            auto payment = TryDetectPayment(tx->vout[i], i, txid, height);
            if (payment) {
                COutPoint outpoint(Txid::FromUint256(txid), i);

                // Update height if we already have it from mempool
                auto it = m_payments.find(outpoint);
                if (it != m_payments.end()) {
                    it->second.blockHeight = height;
                } else {
                    m_payments[outpoint] = *payment;
                    m_paymentKeys[outpoint] = payment->derivedPrivKey;
                }

                payments.push_back(*payment);
            }
        }
    }

    return payments;
}

std::vector<CStealthPayment> CStealthAddressManager::GetStealthPayments(bool includeSpent) const
{
    LOCK(cs_stealth);
    std::vector<CStealthPayment> result;
    for (const auto& [outpoint, payment] : m_payments) {
        if (includeSpent || !payment.spent) {
            result.push_back(payment);
        }
    }
    return result;
}

std::vector<CStealthPayment> CStealthAddressManager::GetUnspentStealthOutputs() const
{
    LOCK(cs_stealth);
    std::vector<CStealthPayment> result;
    for (const auto& [outpoint, payment] : m_payments) {
        if (!payment.spent) {
            result.push_back(payment);
        }
    }
    return result;
}

bool CStealthAddressManager::MarkSpent(const COutPoint& outpoint, const uint256& spendingTx)
{
    LOCK(cs_stealth);
    auto it = m_payments.find(outpoint);
    if (it == m_payments.end()) {
        return false;
    }

    it->second.spent = true;

    LogPrintf("Marked stealth output as spent: %s:%d in tx %s\n",
              outpoint.hash.ToString(), outpoint.n, spendingTx.ToString());
    return true;
}

CAmount CStealthAddressManager::GetStealthBalance() const
{
    LOCK(cs_stealth);
    CAmount total = 0;
    for (const auto& [outpoint, payment] : m_payments) {
        if (!payment.spent) {
            total += payment.nValue;
        }
    }
    return total;
}

CAmount CStealthAddressManager::GetSpendableStealthBalance() const
{
    LOCK(cs_stealth);
    CAmount total = 0;
    for (const auto& [outpoint, payment] : m_payments) {
        if (!payment.spent && payment.blockHeight > 0) {
            // Confirmed payments only
            total += payment.nValue;
        }
    }
    return total;
}

bool CStealthAddressManager::CreateStealthOutput(
    const privacy::CStealthAddress& recipientAddress,
    CAmount amount,
    CTxOut& txout,
    privacy::CStealthOutput& stealthData)
{
    // Generate ephemeral key for this payment
    CKey ephemeralKey;
    ephemeralKey.MakeNewKey(true);

    // Generate the stealth destination
    if (!privacy::GenerateStealthDestination(recipientAddress, ephemeralKey, stealthData)) {
        LogPrintf("Failed to generate stealth destination\n");
        return false;
    }

    // Create P2PK output with the one-time public key
    txout.scriptPubKey = GetScriptForRawPubKey(stealthData.oneTimePubKey);
    txout.nValue = amount;

    LogPrintf("Created stealth output: pubkey=%s, amount=%d\n",
              HexStr(stealthData.oneTimePubKey), amount);
    return true;
}

std::optional<CKey> CStealthAddressManager::GetPrivateKeyForOutput(const COutPoint& outpoint) const
{
    LOCK(cs_stealth);
    auto it = m_paymentKeys.find(outpoint);
    if (it != m_paymentKeys.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool CStealthAddressManager::LoadFromDB()
{
    // Simplified loading - in production would use proper wallet database
    // For now, just log that we'd load from database
    LogPrintf("Stealth address manager: load from DB (stub)\n");
    return true;
}

bool CStealthAddressManager::SaveToDB()
{
    // Simplified saving - in production would use proper wallet database
    LogPrintf("Stealth address manager: save to DB (stub)\n");
    return true;
}

bool CStealthAddressManager::HasStealthAddresses() const
{
    LOCK(cs_stealth);
    return !m_stealthAddresses.empty();
}

size_t CStealthAddressManager::GetStealthAddressCount() const
{
    LOCK(cs_stealth);
    return m_stealthAddresses.size();
}

uint256 CStealthAddressManager::HashStealthAddress(const privacy::CStealthAddress& addr)
{
    HashWriter hasher;
    hasher << addr.scanPubKey << addr.spendPubKey;
    return hasher.GetHash();
}

//
// StealthAddressDB Implementation (stub - uses memory for now)
//

bool StealthAddressDB::WriteStealthAddress(WalletBatch& batch, const CStealthAddressData& addressData)
{
    // Stub - would write to wallet database
    (void)batch;
    (void)addressData;
    return true;
}

bool StealthAddressDB::ReadStealthAddresses(WalletDatabase& db,
                                             std::map<uint256, CStealthAddressData>& addresses)
{
    // Stub - would read from wallet database
    (void)db;
    addresses.clear();
    return true;
}

bool StealthAddressDB::WriteStealthPayment(WalletBatch& batch, const CStealthPayment& payment)
{
    // Stub
    (void)batch;
    (void)payment;
    return true;
}

bool StealthAddressDB::ReadStealthPayments(WalletDatabase& db,
                                            std::map<COutPoint, CStealthPayment>& payments)
{
    // Stub
    (void)db;
    payments.clear();
    return true;
}

bool StealthAddressDB::WriteStealthKey(WalletBatch& batch, const COutPoint& outpoint, const CKey& key)
{
    // Stub
    (void)batch;
    (void)outpoint;
    (void)key;
    return true;
}

bool StealthAddressDB::ReadStealthKey(WalletDatabase& db, const COutPoint& outpoint, CKey& key)
{
    // Stub
    (void)db;
    (void)outpoint;
    (void)key;
    return false;
}

} // namespace wallet
