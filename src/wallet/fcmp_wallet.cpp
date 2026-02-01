// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/fcmp_wallet.h>
#include <wallet/wallet.h>
#include <privacy/ed25519/pedersen.h>
#include <privacy/stealth.h>
#include <hash.h>
#include <logging.h>
#include <util/time.h>

#include <algorithm>

namespace wallet {

// ============================================================================
// Constructor / Destructor
// ============================================================================

CFcmpWalletManager::CFcmpWalletManager(CWallet* wallet)
    : m_wallet(wallet)
{
    // Curve tree will be set separately during initialization
}

CFcmpWalletManager::~CFcmpWalletManager() = default;

// ============================================================================
// Transaction Creation
// ============================================================================

CFcmpTransactionResult CFcmpWalletManager::CreateFcmpTransaction(
    const std::vector<CFcmpRecipient>& recipients,
    const CFcmpTransactionParams& params)
{
    CFcmpTransactionResult result;
    result.success = false;

    LOCK(cs_fcmp);

    // Validate recipients
    if (recipients.empty()) {
        result.error = "No recipients specified";
        return result;
    }

    // Calculate total output amount
    CAmount totalOutput = 0;
    for (const auto& recipient : recipients) {
        if (recipient.amount <= 0) {
            result.error = "Invalid recipient amount";
            return result;
        }
        totalOutput += recipient.amount;
    }

    // Estimate fee if not fixed
    CAmount fee = params.fixedFee;
    if (fee == 0) {
        // Estimate based on typical FCMP transaction size
        fee = EstimateFee(2, recipients.size() + 1, params.feeRate);
    }

    // Calculate target amount
    CAmount targetAmount = totalOutput + fee;
    if (params.subtractFeeFromAmount && recipients.size() == 1) {
        targetAmount = totalOutput;
    }

    // Select inputs
    std::vector<CFcmpOutputInfo> selectedInputs;
    CAmount inputTotal = 0;
    if (!SelectInputs(targetAmount, selectedInputs, inputTotal, params.minConfirmations)) {
        result.error = "Insufficient FCMP funds";
        return result;
    }

    // Verify we have enough
    if (inputTotal < targetAmount) {
        result.error = "Selected inputs insufficient for amount + fee";
        return result;
    }

    // Adjust fee if subtracting from amount
    if (params.subtractFeeFromAmount && recipients.size() == 1) {
        // Don't change - fee comes from output
    }

    // Compute message hash for signatures
    uint256 messageHash = ComputeMessageHash(selectedInputs, recipients, fee);

    // Check we have a curve tree
    if (!m_curveTree) {
        result.error = "Curve tree not initialized";
        return result;
    }

    // Build privacy transaction
    result.privacyTx.privacyType = privacy::PrivacyType::FCMP;
    result.privacyTx.nFee = fee;
    result.fee = fee;

    // Build FCMP inputs
    for (const auto& input : selectedInputs) {
        auto fcmpInput = BuildFcmpInput(input, messageHash);
        if (!fcmpInput) {
            result.error = "Failed to build FCMP input";
            return result;
        }

        result.privacyTx.fcmpInputs.push_back(*fcmpInput);
        result.keyImages.push_back(fcmpInput->keyImage);
    }

    // Build outputs
    CAmount changeAmount = inputTotal - totalOutput - fee;

    for (size_t i = 0; i < recipients.size(); ++i) {
        const auto& recipient = recipients[i];
        CAmount outputAmount = recipient.amount;

        // Subtract fee from first output if requested
        if (params.subtractFeeFromAmount && i == 0) {
            outputAmount -= fee;
            if (outputAmount <= 0) {
                result.error = "Amount too small after fee subtraction";
                return result;
            }
        }

        // Create output
        privacy::CPrivacyOutput privOutput;

        // Generate stealth output
        CKey ephemeralKey;
        ephemeralKey.MakeNewKey(true);
        privacy::GenerateStealthDestination(
            recipient.stealthAddress,
            ephemeralKey,
            privOutput.stealthOutput
        );

        // Create commitment
        ed25519::Scalar blinding = ed25519::Scalar::Random();
        auto commitment = ed25519::PedersenCommitment::CommitAmount(
            static_cast<uint64_t>(outputAmount),
            blinding
        );

        // Store in output
        privOutput.confidentialOutput.commitment.data.resize(33);
        privOutput.confidentialOutput.commitment.data[0] = 0x02;
        std::memcpy(
            privOutput.confidentialOutput.commitment.data.data() + 1,
            commitment.GetPoint().data.data(),
            32
        );
        privOutput.nValue = outputAmount;

        result.privacyTx.privacyOutputs.push_back(privOutput);
    }

    // Add change output if needed
    if (changeAmount > 0) {
        // Get our own stealth address for change
        // For now, create a simple change output
        privacy::CPrivacyOutput changeOutput;

        // Generate random key for change (we'll track this separately)
        ed25519::Scalar changeBlinding = ed25519::Scalar::Random();
        auto changeCommitment = ed25519::PedersenCommitment::CommitAmount(
            static_cast<uint64_t>(changeAmount),
            changeBlinding
        );

        changeOutput.confidentialOutput.commitment.data.resize(33);
        changeOutput.confidentialOutput.commitment.data[0] = 0x02;
        std::memcpy(
            changeOutput.confidentialOutput.commitment.data.data() + 1,
            changeCommitment.GetPoint().data.data(),
            32
        );
        changeOutput.nValue = changeAmount;

        result.privacyTx.privacyOutputs.push_back(changeOutput);

        // TODO: Create proper change output with stealth address
        // and add to our tracked outputs
    }

    // Verify the transaction
    if (!result.privacyTx.Verify()) {
        result.error = "Transaction verification failed";
        return result;
    }

    // Convert to standard transaction for broadcast
    result.standardTx = MakeTransactionRef(result.privacyTx.ToTransaction());

    result.success = true;
    return result;
}

CAmount CFcmpWalletManager::EstimateFee(
    size_t numInputs,
    size_t numOutputs,
    const CFeeRate& feeRate) const
{
    // FCMP proof is approximately:
    // - Per input: ~2KB (membership proof + SA+L signature)
    // - Per output: ~100 bytes (commitment + encrypted data)
    // - Base overhead: ~100 bytes

    size_t estimatedSize = 100 +
                          numInputs * 2048 +
                          numOutputs * 100;

    return feeRate.GetFee(estimatedSize);
}

CFcmpShieldResult CFcmpWalletManager::CreateShieldTransaction(
    const privacy::CStealthAddress& recipient,
    CAmount amount,
    int minConfirmations)
{
    CFcmpShieldResult result;
    result.success = false;

    LOCK(cs_fcmp);

    if (amount <= 0) {
        result.error = "Invalid amount";
        return result;
    }

    // Estimate fee for shielding transaction
    // Shield txs are simpler: transparent inputs -> FCMP output in OP_RETURN
    CAmount fee = 1000; // 0.00001 WATTx minimum fee

    // Create a standard transaction that:
    // 1. Spends transparent inputs
    // 2. Has an OP_RETURN output with FCMP output data (O, I, C)
    // 3. May have change output back to wallet

    // Generate output tuple for curve tree
    ed25519::Scalar blinding = ed25519::Scalar::Random();
    std::optional<ed25519::Scalar> privKey;
    curvetree::OutputTuple outputTuple = CreateOutputTuple(recipient, amount, blinding, privKey);

    // Create the OP_RETURN script with FCMP output marker
    // Format: OP_RETURN <FCMP_MARKER> <O:32> <I:32> <C:32>
    CScript opReturnScript;
    opReturnScript << OP_RETURN;

    std::vector<uint8_t> fcmpData;
    fcmpData.reserve(4 + 96); // marker + 3 points

    // FCMP marker "FCMP"
    fcmpData.push_back(0x46); // 'F'
    fcmpData.push_back(0x43); // 'C'
    fcmpData.push_back(0x4D); // 'M'
    fcmpData.push_back(0x50); // 'P'

    // Add O, I, C points
    fcmpData.insert(fcmpData.end(), outputTuple.O.data.begin(), outputTuple.O.data.end());
    fcmpData.insert(fcmpData.end(), outputTuple.I.data.begin(), outputTuple.I.data.end());
    fcmpData.insert(fcmpData.end(), outputTuple.C.data.begin(), outputTuple.C.data.end());

    opReturnScript << fcmpData;

    // Build the transaction
    CMutableTransaction mtx;
    mtx.version = 2;

    // Add OP_RETURN output (FCMP data)
    mtx.vout.push_back(CTxOut(0, opReturnScript));

    // The wallet will add inputs and change output
    // For now, return a template that the wallet can complete

    result.standardTx = MakeTransactionRef(std::move(mtx));
    result.fee = fee;

    // If this is our own stealth address, track the output
    if (privKey) {
        CFcmpOutputInfo outputInfo;
        outputInfo.amount = amount;
        outputInfo.privKey = *privKey;
        outputInfo.blinding = blinding;
        outputInfo.outputTuple = outputTuple;
        outputInfo.blockHeight = -1; // Unconfirmed
        outputInfo.spent = false;
        outputInfo.nTime = GetTime();

        // Generate key image
        auto keyImage = GenerateKeyImage(*privKey, outputTuple.O);
        outputInfo.keyImageHash = keyImage.GetHash();

        // Leaf index will be assigned when added to tree
        if (m_curveTree) {
            result.leafIndex = m_curveTree->GetOutputCount(); // Next index
        }
        outputInfo.treeLeafIndex = result.leafIndex;

        // Note: Output will be added when transaction confirms
        // For now, store pending info
    }

    result.success = true;
    return result;
}

// ============================================================================
// Output Management
// ============================================================================

std::vector<CFcmpOutputInfo> CFcmpWalletManager::GetFcmpOutputs(bool includeSpent) const
{
    LOCK(cs_fcmp);

    std::vector<CFcmpOutputInfo> outputs;
    outputs.reserve(m_fcmpOutputs.size());

    for (const auto& [outpoint, info] : m_fcmpOutputs) {
        if (includeSpent || !info.spent) {
            outputs.push_back(info);
        }
    }

    return outputs;
}

std::vector<CFcmpOutputInfo> CFcmpWalletManager::GetSpendableFcmpOutputs(int minConfirmations) const
{
    LOCK(cs_fcmp);

    int currentHeight = GetCurrentHeight();
    std::vector<CFcmpOutputInfo> outputs;

    for (const auto& [outpoint, info] : m_fcmpOutputs) {
        if (info.IsSpendable(currentHeight, minConfirmations)) {
            outputs.push_back(info);
        }
    }

    // Sort by amount (largest first for efficient selection)
    std::sort(outputs.begin(), outputs.end(),
        [](const CFcmpOutputInfo& a, const CFcmpOutputInfo& b) {
            return a.amount > b.amount;
        });

    return outputs;
}

bool CFcmpWalletManager::AddFcmpOutput(const CFcmpOutputInfo& output)
{
    LOCK(cs_fcmp);

    if (m_fcmpOutputs.count(output.outpoint)) {
        return false; // Already exists
    }

    m_fcmpOutputs[output.outpoint] = output;

    // Track key image
    if (!output.keyImageHash.IsNull()) {
        m_keyImages[output.keyImageHash] = output.outpoint;
    }

    LogPrintf("FCMP: Added output %s: %d satoshis at leaf %lu\n",
              output.outpoint.ToString(), output.amount, output.treeLeafIndex);

    return true;
}

bool CFcmpWalletManager::MarkFcmpOutputSpent(const COutPoint& outpoint, const uint256& spendingTxHash)
{
    LOCK(cs_fcmp);

    auto it = m_fcmpOutputs.find(outpoint);
    if (it == m_fcmpOutputs.end()) {
        return false;
    }

    it->second.spent = true;

    // Track the spending
    if (!it->second.keyImageHash.IsNull()) {
        m_spentKeyImages[it->second.keyImageHash] = spendingTxHash;
    }

    LogPrintf("FCMP: Marked output %s as spent in tx %s\n",
              outpoint.ToString(), spendingTxHash.ToString());

    return true;
}

bool CFcmpWalletManager::HaveFcmpOutput(const COutPoint& outpoint) const
{
    LOCK(cs_fcmp);
    return m_fcmpOutputs.count(outpoint) > 0;
}

std::optional<CFcmpOutputInfo> CFcmpWalletManager::GetFcmpOutput(const COutPoint& outpoint) const
{
    LOCK(cs_fcmp);

    auto it = m_fcmpOutputs.find(outpoint);
    if (it == m_fcmpOutputs.end()) {
        return std::nullopt;
    }

    return it->second;
}

// ============================================================================
// Key Image Management
// ============================================================================

bool CFcmpWalletManager::IsKeyImageSpent(const privacy::CKeyImage& keyImage) const
{
    LOCK(cs_fcmp);

    uint256 hash = keyImage.GetHash();
    return m_spentKeyImages.count(hash) > 0;
}

privacy::CKeyImage CFcmpWalletManager::GenerateKeyImage(
    const ed25519::Scalar& privKey,
    const ed25519::Point& outputPoint) const
{
    // Compute Hp(O) - hash of output to point
    std::vector<uint8_t> toHash(outputPoint.data.begin(), outputPoint.data.end());
    auto Hp = ed25519::Point::HashToPoint(toHash);

    // Key image I = x * Hp(O)
    auto I = privKey * Hp;

    // Convert to CKeyImage format
    privacy::CKeyImage keyImage;
    keyImage.data.resize(33);
    keyImage.data[0] = 0x02; // Ed25519 prefix
    std::memcpy(keyImage.data.data() + 1, I.data.data(), 32);

    return keyImage;
}

// ============================================================================
// Balance Queries
// ============================================================================

CAmount CFcmpWalletManager::GetFcmpBalance() const
{
    LOCK(cs_fcmp);

    CAmount total = 0;
    for (const auto& [outpoint, info] : m_fcmpOutputs) {
        if (!info.spent) {
            total += info.amount;
        }
    }

    return total;
}

CAmount CFcmpWalletManager::GetSpendableFcmpBalance(int minConfirmations) const
{
    LOCK(cs_fcmp);

    int currentHeight = GetCurrentHeight();
    CAmount total = 0;

    for (const auto& [outpoint, info] : m_fcmpOutputs) {
        if (info.IsSpendable(currentHeight, minConfirmations)) {
            total += info.amount;
        }
    }

    return total;
}

CAmount CFcmpWalletManager::GetPendingFcmpBalance() const
{
    LOCK(cs_fcmp);

    CAmount total = 0;
    for (const auto& [outpoint, info] : m_fcmpOutputs) {
        if (!info.spent && info.blockHeight < 0) {
            total += info.amount;
        }
    }

    return total;
}

// ============================================================================
// Curve Tree Access
// ============================================================================

std::shared_ptr<curvetree::CurveTree> CFcmpWalletManager::GetCurveTree() const
{
    LOCK(cs_fcmp);
    return m_curveTree;
}

void CFcmpWalletManager::SetCurveTree(std::shared_ptr<curvetree::CurveTree> tree)
{
    LOCK(cs_fcmp);
    m_curveTree = std::move(tree);
}

ed25519::Point CFcmpWalletManager::GetTreeRoot() const
{
    LOCK(cs_fcmp);

    if (!m_curveTree) {
        return ed25519::Point::Identity();
    }

    return m_curveTree->GetRoot();
}

// ============================================================================
// Transaction Scanning
// ============================================================================

int CFcmpWalletManager::ScanTransactionForFcmpOutputs(
    const CTransaction& tx,
    int blockHeight)
{
    // TODO: Implement scanning for FCMP outputs
    // This would parse the transaction for privacy data and check if
    // any outputs belong to our stealth addresses
    return 0;
}

int CFcmpWalletManager::ScanBlockForFcmpOutputs(
    const CBlock& block,
    int blockHeight)
{
    int found = 0;
    for (const auto& tx : block.vtx) {
        found += ScanTransactionForFcmpOutputs(*tx, blockHeight);
    }
    return found;
}

// ============================================================================
// Persistence
// ============================================================================

bool CFcmpWalletManager::Load()
{
    // TODO: Load from wallet database
    // WalletBatch batch(m_wallet->GetDatabase());
    // batch.ReadFcmpOutputs(m_fcmpOutputs);
    // batch.ReadFcmpKeyImages(m_keyImages, m_spentKeyImages);
    return true;
}

bool CFcmpWalletManager::Save()
{
    // TODO: Save to wallet database
    // WalletBatch batch(m_wallet->GetDatabase());
    // batch.WriteFcmpOutputs(m_fcmpOutputs);
    // batch.WriteFcmpKeyImages(m_keyImages, m_spentKeyImages);
    return true;
}

// ============================================================================
// Utility
// ============================================================================

int CFcmpWalletManager::GetCurrentHeight() const
{
    if (!m_wallet) return 0;

    LOCK(m_wallet->cs_wallet);
    return m_wallet->GetLastBlockHeight();
}

curvetree::OutputTuple CFcmpWalletManager::CreateOutputTuple(
    const privacy::CStealthAddress& stealthAddr,
    CAmount amount,
    ed25519::Scalar& blinding,
    std::optional<ed25519::Scalar>& privKey) const
{
    curvetree::OutputTuple tuple;

    // Generate ephemeral key
    CKey ephemeralKey;
    ephemeralKey.MakeNewKey(true);

    // Derive one-time address
    // O = Hs(r*V)*G + S where V is view pubkey, S is spend pubkey
    // For Ed25519, we use similar derivation

    // Simplified: Use random point for now
    // TODO: Proper stealth address derivation for Ed25519
    auto kp = ed25519::KeyPair::Generate();
    tuple.O = kp.public_key;

    // If we're sending to ourselves, store the private key
    // (in practice, check if we own the stealth address)
    privKey = std::nullopt;

    // I = Hp(O) - key image base
    std::vector<uint8_t> toHash(tuple.O.data.begin(), tuple.O.data.end());
    tuple.I = ed25519::Point::HashToPoint(toHash);

    // C = amount*H + blinding*G
    blinding = ed25519::Scalar::Random();
    auto commitment = ed25519::PedersenCommitment::CommitAmount(
        static_cast<uint64_t>(amount),
        blinding
    );
    tuple.C = commitment.GetPoint();

    return tuple;
}

// ============================================================================
// Private Methods
// ============================================================================

bool CFcmpWalletManager::SelectInputs(
    CAmount targetAmount,
    std::vector<CFcmpOutputInfo>& selectedInputs,
    CAmount& inputTotal,
    int minConfirmations)
{
    AssertLockHeld(cs_fcmp);

    selectedInputs.clear();
    inputTotal = 0;

    // Get spendable outputs sorted by amount (largest first)
    auto spendable = GetSpendableFcmpOutputs(minConfirmations);

    // Simple selection: take largest outputs until we have enough
    for (const auto& output : spendable) {
        selectedInputs.push_back(output);
        inputTotal += output.amount;

        if (inputTotal >= targetAmount) {
            return true;
        }
    }

    // Not enough funds
    return false;
}

std::optional<privacy::CFcmpInput> CFcmpWalletManager::BuildFcmpInput(
    const CFcmpOutputInfo& output,
    const uint256& messageHash)
{
    AssertLockHeld(cs_fcmp);

    if (!m_curveTree) {
        return std::nullopt;
    }

    privacy::CFcmpInput fcmpInput;

    // Generate key image
    fcmpInput.keyImage = GenerateKeyImage(output.privKey, output.outputTuple.O);

    // Re-randomize input
    ed25519::Scalar rerandomizer = ed25519::Scalar::Random();

    auto G = ed25519::Point::BasePoint();
    auto H = ed25519::PedersenGenerators::Default().H();

    // O_tilde = O + r*G
    auto rG = rerandomizer * G;
    fcmpInput.inputTuple.O_tilde = output.outputTuple.O + rG;

    // I_tilde = I (cannot re-randomize key image)
    fcmpInput.inputTuple.I_tilde = output.outputTuple.I;

    // R = r*G
    fcmpInput.inputTuple.R = rG;

    // C_tilde = C + r*H
    auto rH = rerandomizer * H;
    fcmpInput.inputTuple.C_tilde = output.outputTuple.C + rH;

    // Generate membership proof
    auto branch = m_curveTree->GetBranch(output.treeLeafIndex);
    if (!branch) {
        LogPrintf("FCMP: Failed to get branch for leaf %lu\n",
                  output.treeLeafIndex);
        return std::nullopt;
    }

#ifdef HAVE_FCMP
    try {
        privacy::fcmp::FcmpProver prover(m_curveTree);
        auto proofBytes = prover.GenerateProof(output.outputTuple, output.treeLeafIndex);
        fcmpInput.membershipProof = privacy::CFcmpProof(
            std::move(proofBytes),
            m_curveTree->GetRoot()
        );
    } catch (const std::exception& e) {
        LogPrintf("FCMP: Proof generation failed: %s\n", e.what());
        return std::nullopt;
    }
#else
    // Placeholder proof for testing
    fcmpInput.membershipProof.version = 1;
    fcmpInput.membershipProof.treeRoot = m_curveTree->GetRoot();
    fcmpInput.membershipProof.proofData.resize(64, 0);
    HashWriter hasher{};
    hasher << output.treeLeafIndex;
    hasher << output.outputTuple.O.data;
    uint256 proofHash = hasher.GetHash();
    std::memcpy(fcmpInput.membershipProof.proofData.data(), proofHash.begin(), 32);
#endif

    // Generate SA+L signature
    // c = H(R || I_tilde || O_tilde || message)
    HashWriter sigHasher{};
    sigHasher << fcmpInput.inputTuple.R.data;
    sigHasher << fcmpInput.inputTuple.I_tilde.data;
    sigHasher << fcmpInput.inputTuple.O_tilde.data;
    sigHasher << messageHash;
    uint256 challengeHash = sigHasher.GetHash();

    fcmpInput.salSignature.c = ed25519::Scalar::FromBytesModOrder(
        std::vector<uint8_t>(challengeHash.begin(), challengeHash.end())
    );

    // s = r + c*x
    auto cx = fcmpInput.salSignature.c * output.privKey;
    fcmpInput.salSignature.s = rerandomizer + cx;

    // Create pseudo-output commitment
    auto pseudoCommitment = ed25519::PedersenCommitment::CommitAmount(
        static_cast<uint64_t>(output.amount),
        output.blinding
    );
    fcmpInput.pseudoOutput.data.resize(33);
    fcmpInput.pseudoOutput.data[0] = 0x02;
    std::memcpy(
        fcmpInput.pseudoOutput.data.data() + 1,
        pseudoCommitment.GetPoint().data.data(),
        32
    );

    return fcmpInput;
}

uint256 CFcmpWalletManager::ComputeMessageHash(
    const std::vector<CFcmpOutputInfo>& inputs,
    const std::vector<CFcmpRecipient>& recipients,
    CAmount fee) const
{
    HashWriter hasher{};

    // Hash inputs
    for (const auto& input : inputs) {
        hasher << input.outpoint;
        hasher << input.amount;
        hasher << input.treeLeafIndex;
    }

    // Hash outputs
    for (const auto& recipient : recipients) {
        hasher << recipient.amount;
        // Hash stealth address components
        hasher << recipient.stealthAddress.scanPubKey;
        hasher << recipient.stealthAddress.spendPubKey;
    }

    hasher << fee;

    return hasher.GetHash();
}

} // namespace wallet
