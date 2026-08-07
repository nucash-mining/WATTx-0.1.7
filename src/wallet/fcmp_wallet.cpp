// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/fcmp_wallet.h>
#include <wallet/coincontrol.h>
#include <wallet/receive.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <privacy/fcmp/fcmp_wrapper.h>
#include <privacy/fcmp_consensus.h>  // DecodeFcmpTransaction, GetShieldedPoolScript
#include <privacy/ed25519/pedersen.h>
#include <privacy/stealth.h>
#include <hash.h>
#include <chainparams.h>
#include <logging.h>
#include <script/script.h>
#include <util/moneystr.h>
#include <util/time.h>

#include <algorithm>

namespace wallet {

namespace {

//! Convert an ed25519 blinding scalar into the CBlindingFactor container the
//! confidential layer takes. Both sides reduce mod the group order, and an
//! already-reduced scalar round-trips unchanged.
privacy::CBlindingFactor ToBlindingFactor(const ed25519::Scalar& s)
{
    const std::vector<uint8_t> b = s.GetBytes();
    uint256 u;
    if (b.size() == 32) std::memcpy(u.begin(), b.data(), 32);
    return privacy::CBlindingFactor(u);
}

//! Build a commitment through the confidential layer rather than by hand.
//!
//! Every site that used to assemble the 33-byte buffer itself wrote a 0x02 tag,
//! which the ed25519 layer deliberately REJECTS as a legacy secp256k1
//! commitment (see confidential_ed25519.cpp). Nothing the wallet built could
//! pass consensus. Going through CreateCommitment keeps the tag in exactly one
//! place, so the wallet and the verifier cannot drift apart again.
bool MakeCommitment(CAmount amount, const ed25519::Scalar& blinding,
                    privacy::CPedersenCommitment& out)
{
    return privacy::CreateCommitment(amount, ToBlindingFactor(blinding), out);
}

//! Keystream for the note's encrypted amount, from the one-time public key.
//!
//! The recipient reconstructs that key while scanning, so this needs no extra
//! channel -- the same trick the blinding derivation uses, under a different
//! domain separator so the two never coincide.
uint64_t AmountMask(const CPubKey& oneTimePubKey)
{
    std::vector<uint8_t> input(oneTimePubKey.begin(), oneTimePubKey.end());
    input.push_back(0x43); // domain separator for amount encryption
    const uint256 h = Hash(input);
    uint64_t mask = 0;
    for (int i = 0; i < 8; ++i) {
        mask |= static_cast<uint64_t>(h.begin()[i]) << (8 * i);
    }
    return mask;
}
} // namespace

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
    //
    // A default-constructed CFeeRate is ZERO, which produced a zero fee and a
    // transaction the node then refused to relay -- after several seconds of
    // proving. Fall back to what the node will actually accept.
    CFeeRate feeRate = params.feeRate;
    if (feeRate == CFeeRate(0)) {
        feeRate = m_wallet->chain().relayMinFee();
    }

    CAmount fee = params.fixedFee;
    if (fee == 0) {
        // One input is the common case; the count is not known until selection,
        // so size for two and let the surplus be a tip rather than a rejection.
        fee = EstimateFee(2, recipients.size() + 1, feeRate);
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

    // Check we have a curve tree
    if (!m_curveTree) {
        result.error = "Curve tree not initialized";
        return result;
    }

    // Split the recipients: shielded ones become notes with hidden amounts,
    // transparent ones take value out of the pool in the clear (a deshield).
    CAmount deshieldTotal = 0;
    for (const auto& recipient : recipients) {
        if (!recipient.IsStealthRecipient()) {
            deshieldTotal += recipient.amount;
        }
    }

    result.privacyTx.privacyType = privacy::PrivacyType::FCMP;
    result.privacyTx.nFee = fee;
    result.fee = fee;

    // ---- Phase 1: re-randomise the inputs -------------------------------
    //
    // Proving has to wait: the SA+L signature commits to the transaction hash,
    // and the transaction cannot be assembled until its output commitments are
    // known -- and those are balanced against b~ = b + r_c. So r_c has to exist
    // before the message does. Re-randomising first and proving in phase 2 is
    // what breaks that circle.
    struct PreparedInput {
        CFcmpOutputInfo note;
        privacy::fcmp::FcmpProver::Rerandomization rerandomized;
    };
    std::vector<PreparedInput> prepared;
    prepared.reserve(selectedInputs.size());

    std::vector<privacy::CBlindingFactor> inputBlinds;
    inputBlinds.reserve(selectedInputs.size());

    // The G-component the outputs must reproduce: sum over inputs of b + r_c.
    ed25519::Scalar inputBlindSum = ed25519::Scalar::Zero();

    for (const auto& input : selectedInputs) {
        const auto leafIndex = ResolveLeafIndex(input);
        if (!leafIndex) {
            result.error = strprintf("Note %s is not in the curve tree yet; it cannot "
                                     "be spent until the block carrying it connects",
                                     input.outpoint.ToString());
            return result;
        }

        privacy::fcmp::FcmpProver::Rerandomization rr;
        try {
            privacy::fcmp::FcmpContext ctx;
            privacy::fcmp::FcmpProver prover(m_curveTree);
            rr = prover.Rerandomize(*leafIndex);
        } catch (const std::exception& e) {
            result.error = strprintf("Re-randomisation failed: %s", e.what());
            return result;
        }

        const ed25519::Scalar r_c =
            ed25519::Scalar::FromBytesModOrder(rr.c_blind.data(), 32);
        const ed25519::Scalar bTilde = input.blinding + r_c;

        inputBlindSum = inputBlindSum + bTilde;
        inputBlinds.push_back(ToBlindingFactor(bTilde));
        prepared.push_back(PreparedInput{input, std::move(rr)});
    }

    // ---- Build the outputs ----------------------------------------------
    //
    // Blindings are settled BEFORE any commitment is computed, so nothing has to
    // be rebuilt afterwards and no proof can end up bound to a commitment the
    // transaction does not carry.
    //
    // Each note paid to someone else takes a blinding derived from its one-time
    // key, which is the only way its owner can ever recover it. The CHANGE note
    // absorbs whatever is left to make the sums match. That assignment is not
    // interchangeable: the balancing blinding cannot be derived from anything
    // public, so it has to land on the one note we keep.
    CAmount changeAmount = inputTotal - totalOutput - fee;
    if (changeAmount < 0) {
        result.error = "Selected inputs insufficient for amount + fee";
        return result;
    }

    struct ShieldedOutput {
        curvetree::OutputTuple tuple;
        CPubKey ephemeralPubKey;
        CPubKey oneTimePubKey;
        CAmount amount{0};
    };
    std::vector<ShieldedOutput> notes;
    std::vector<privacy::CBlindingFactor> outputBlinds;
    std::vector<CAmount> outputAmounts;
    ed25519::Scalar recipientBlindSum = ed25519::Scalar::Zero();

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

        privacy::CPrivacyOutput privOutput;

        if (recipient.IsStealthRecipient()) {
            ed25519::Scalar blinding;
            std::optional<ed25519::Scalar> outPrivKey;
            CPubKey ephemeralPubKey;
            CPubKey oneTimePubKey;
            curvetree::OutputTuple tuple = CreateOutputTuple(
                recipient.stealthAddress, outputAmount, blinding, outPrivKey,
                ephemeralPubKey, oneTimePubKey,
                BlindingPolicy::DerivedFromOneTimeKey);

            if (!MakeCommitment(outputAmount, blinding,
                                privOutput.confidentialOutput.commitment)) {
                result.error = "Failed to create output commitment";
                return result;
            }

            recipientBlindSum = recipientBlindSum + blinding;
            outputBlinds.push_back(ToBlindingFactor(blinding));
            outputAmounts.push_back(outputAmount);
            notes.push_back(ShieldedOutput{tuple, ephemeralPubKey, oneTimePubKey, outputAmount});
        } else {
            // Deshielding: a transparent output. Its value is explicit and is
            // accounted for by the pool delta, not by a commitment.
            privOutput.scriptPubKey = recipient.scriptPubKey;
        }

        privOutput.nValue = outputAmount;
        result.privacyTx.privacyOutputs.push_back(privOutput);
    }

    // The change note, which always exists -- even at zero value.
    //
    // It is what makes the transaction balance, so it is not optional: with no
    // change note the balancing blinding would have to go on a recipient's note,
    // and the recipient could never reconstruct it. A zero-value note costs an
    // OP_RETURN and nothing else.
    {
        auto* stealthMgr = m_wallet->GetStealthAddressManager();
        if (!stealthMgr || !stealthMgr->HasStealthAddresses()) {
            result.error = "No stealth address available for change; a shielded spend "
                           "needs one to balance against";
            return result;
        }
        const privacy::CStealthAddress changeAddr = stealthMgr->GetStealthAddresses()[0].address;

        // b_change = sum(b_in + r_c) - sum(recipient blindings), so that
        // sum(output blindings) == sum(input blindings) exactly.
        ed25519::Scalar changeBlinding = inputBlindSum - recipientBlindSum;

        std::optional<ed25519::Scalar> changePk;
        CPubKey changeEphemeral;
        CPubKey changeOneTime;
        curvetree::OutputTuple changeTuple = CreateOutputTuple(
            changeAddr, changeAmount, changeBlinding, changePk, changeEphemeral,
            changeOneTime, BlindingPolicy::Explicit);

        privacy::CPrivacyOutput changeOutput;
        if (!MakeCommitment(changeAmount, changeBlinding,
                            changeOutput.confidentialOutput.commitment)) {
            result.error = "Failed to create change commitment";
            return result;
        }
        changeOutput.nValue = changeAmount;
        result.privacyTx.privacyOutputs.push_back(changeOutput);

        outputBlinds.push_back(ToBlindingFactor(changeBlinding));
        outputAmounts.push_back(changeAmount);
        notes.push_back(ShieldedOutput{changeTuple, changeEphemeral, changeOneTime, changeAmount});

        // Remember it, so the wallet can spend it later. The change blinding is
        // the one value in the transaction that cannot be re-derived from public
        // data, so if this record is lost the note is lost with it.
        CFcmpOutputInfo changeInfo;
        changeInfo.amount = changeAmount;
        changeInfo.blinding = changeBlinding;
        changeInfo.outputTuple = changeTuple;
        changeInfo.blockHeight = -1;
        changeInfo.spent = false;
        changeInfo.nTime = GetTime();
        if (changePk) {
            changeInfo.privKey = *changePk;
            changeInfo.keyImageHash =
                GenerateKeyImage(changeInfo.privKey, changeTuple.O).GetHash();
        }
        result.changeOutputInfo = changeInfo;
    }

    // Range-prove the final commitments, in the order they appear.
    {
        std::vector<privacy::CPedersenCommitment> finalCommitments;
        finalCommitments.reserve(outputAmounts.size());
        for (const auto& out : result.privacyTx.privacyOutputs) {
            if (out.confidentialOutput.commitment.IsNull()) continue;
            finalCommitments.push_back(out.confidentialOutput.commitment);
        }
        if (!privacy::CreateAggregatedRangeProof(outputAmounts, outputBlinds,
                                                 finalCommitments,
                                                 result.privacyTx.aggregatedRangeProof)) {
            result.error = "Failed to create the aggregated range proof";
            return result;
        }
    }

    // ---- Assemble the transparent shell ---------------------------------
    //
    // The shell is what carries real coin: it spends pool UTXOs and pays a
    // smaller amount back into the pool, and consensus reads the pool delta from
    // those transparent values rather than from anything the payload claims.
    privacy::CPrivacyTransaction::CFcmpShell shell;
    shell.poolInputs = params.poolInputs;

    for (const auto& note : notes) {
        shell.outputs.emplace_back(0, BuildNoteScript(note.tuple, note.ephemeralPubKey,
                                                      note.oneTimePubKey, note.amount));
    }
    for (const auto& recipient : recipients) {
        if (!recipient.IsStealthRecipient()) {
            shell.outputs.emplace_back(recipient.amount, recipient.scriptPubKey);
        }
    }

    const CAmount poolReturn = params.poolTotal - fee - deshieldTotal;
    if (poolReturn < 0) {
        result.error = strprintf("The selected pool outputs (%s) cannot cover the fee "
                                 "and deshielded value (%s)",
                                 FormatMoney(params.poolTotal),
                                 FormatMoney(fee + deshieldTotal));
        return result;
    }
    shell.outputs.emplace_back(poolReturn, privacy::GetShieldedPoolScript());

    // ---- Phase 2: prove, against the hash of the assembled shell ---------
    //
    // The message MUST be what consensus recomputes -- Hash(tx.GetHash()) -- or
    // no proof this wallet builds can ever verify. It used to be a private hash
    // over the inputs and recipients, which consensus had no way to reconstruct.
    //
    // Witness data is excluded from the txid, so the payload can be attached
    // afterwards without disturbing the hash it commits to. That is also what
    // stops a third party rewriting the outputs of a transaction paying from an
    // anyone-can-spend pool script.
    const uint256 shellTxid = result.privacyTx.ShellTxid(shell);
    HashWriter messageHasher{};
    messageHasher << shellTxid;
    const uint256 messageHash = messageHasher.GetHash();

    for (auto& p : prepared) {
        auto fcmpInput = BuildFcmpInput(p.note, p.rerandomized, messageHash);
        if (!fcmpInput) {
            result.error = "Failed to build FCMP input";
            return result;
        }
        result.privacyTx.fcmpInputs.push_back(*fcmpInput);
        result.keyImages.push_back(fcmpInput->keyImage);
        result.spentOutpoints.push_back(p.note.outpoint);
    }

    // Self-verify exactly what consensus will: the membership proofs and SA+L
    // signatures through the audited verifier, the aggregated range proof, and
    // value conservation against the pool delta.
    //
    // The delta is what the shell expresses transparently: the pool loses the fee
    // and anything deshielded.
    const CAmount poolDelta = -(fee + deshieldTotal);
    if (!result.privacyTx.VerifyFcmpSelfCheck(poolDelta, m_curveTree->GetRoot(),
                                              messageHash)) {
        result.error = "Transaction self-check failed";
        return result;
    }

    auto assembled = result.privacyTx.ToFcmpTransaction(shell);
    if (!assembled) {
        result.error = "Failed to assemble the FCMP transaction";
        return result;
    }

    // The payload rides in the witness, which the txid excludes -- so attaching
    // it must not have changed the hash the proofs commit to. If it did, every
    // proof in this transaction is bound to a message consensus will not
    // recompute, and it would be rejected for reasons pointing nowhere near here.
    if (assembled->GetHash() != shellTxid) {
        result.error = "Assembled transaction does not hash to what was signed";
        return result;
    }

    // The fee was committed to before the transaction could be measured, so
    // confirm the estimate actually covered it. Failing here is a bug in
    // EstimateVirtualSize, and saying so is far more useful than letting the node
    // reject the transaction with "min relay fee not met" once the proofs have
    // already been generated and the change note recorded.
    const size_t actualVsize = GetVirtualTransactionSize(*assembled);
    const CAmount requiredFee = feeRate.GetFee(actualVsize);
    if (fee < requiredFee) {
        result.error = strprintf(
            "Fee %s does not meet the %s required for %d vbytes; "
            "raise the fee rate or pass a fixed fee",
            FormatMoney(fee), FormatMoney(requiredFee), actualVsize);
        return result;
    }

    // The change note's outpoint, now that the txid exists. Notes occupy the
    // first vout slots in shell order, and change is the last of them.
    //
    // The caller used to compute this as privacyOutputs.size() - 1, which is a
    // different sequence entirely -- it counts transparent outputs and excludes
    // the pool output. It agreed only for the simplest shape, and pointed at the
    // wrong output the moment a transaction deshielded anything.
    if (result.changeOutputInfo) {
        result.changeOutputInfo->outpoint =
            COutPoint(Txid::FromUint256(shellTxid), static_cast<uint32_t>(notes.size() - 1));
    }

    result.standardTx = MakeTransactionRef(*assembled);
    result.success = true;
    return result;
}

CAmount CFcmpWalletManager::EstimateFee(
    size_t numInputs,
    size_t numOutputs,
    const CFeeRate& feeRate) const
{
    return feeRate.GetFee(EstimateVirtualSize(numInputs, numOutputs));
}

size_t CFcmpWalletManager::EstimateVirtualSize(size_t numInputs, size_t numOutputs)
{
    // An FCMP spend is mostly witness: the payload -- membership proofs, range
    // proof, commitments -- rides in vin[0]'s witness, which the txid excludes
    // and which is discounted 4:1 when sizing.
    //
    // Split base from witness rather than sizing the whole thing at full weight.
    // Charging witness bytes at 4x overestimated the fee severalfold; sizing a
    // proof at 2KB when it is nearer 4.5KB underestimated it, and the transaction
    // was then rejected for not meeting the relay minimum after the proofs had
    // already been generated.
    //
    // A note output is an OP_RETURN of 133 bytes plus script and amount overhead;
    // the pool output is a 34-byte witness program. Both are base bytes.
    const size_t baseSize =
        10 +                        // version, counts, locktime
        numInputs * 41 +            // outpoint + empty scriptSig + sequence
        (numOutputs + 1) * 145 +    // note OP_RETURNs, plus the pool output
        43;

    // Per input: the FCMP++ proof (~4.4 KB for one layer) and its SA+L part.
    // Per output: a 33-byte commitment and the surrounding serialisation.
    // Plus one aggregated Bulletproofs+ range proof, which grows logarithmically
    // in the number of outputs -- ~700 bytes covers the sizes reachable here.
    const size_t witnessSize =
        numInputs * 5120 +
        (numOutputs + 1) * 128 +
        768;

    // Round up, and leave headroom: paying slightly over the minimum costs a few
    // satoshis, while falling under it wastes several seconds of proving.
    const size_t weight = baseSize * 4 + witnessSize;
    return (weight + 3) / 4 + 64;
}

// ============================================================================
// Auto-Shielding
// ============================================================================

void CFcmpWalletManager::AutoShield()
{
    // Minimum transparent balance to trigger auto-shield (1 WATTx)
    static constexpr CAmount AUTO_SHIELD_MIN = 100000000;

    LOCK(cs_fcmp);

    // Skip if already shielding
    if (m_autoShieldPending) return;

    // Skip during IBD
    if (m_wallet->chain().isInitialBlockDownload()) return;

    // Skip if wallet is locked
    if (m_wallet->IsLocked()) return;

    // Skip if FCMP is not yet active
    int currentHeight = m_wallet->chain().getHeight().value_or(0);
    if (!Params().GetConsensus().IsFcmpActive(currentHeight)) return;

    // Check transparent balance
    Balance bal = GetBalance(*m_wallet, /*min_depth=*/1);
    CAmount transparentBalance = bal.m_mine_trusted;

    if (transparentBalance < AUTO_SHIELD_MIN) return;

    // Get or generate a stealth address for shielding
    auto* stealthMgr = m_wallet->GetStealthAddressManager();
    if (!stealthMgr) return;

    privacy::CStealthAddress shieldAddr;
    if (stealthMgr->HasStealthAddresses()) {
        auto addresses = stealthMgr->GetStealthAddresses();
        if (!addresses.empty()) {
            shieldAddr = addresses[0].address;
        }
    }

    if (!shieldAddr.IsValid()) {
        CStealthAddressData newAddr;
        if (!stealthMgr->GenerateStealthAddress("auto_shield", newAddr)) return;
        shieldAddr = newAddr.address;
    }

    m_autoShieldPending = true;

    // Create shield template to get the OP_RETURN script and cryptographic material
    auto result = CreateShieldTransaction(shieldAddr, transparentBalance - 1000, /*minConfirmations=*/1);

    if (result.success) {
        // Extract OP_RETURN script from template
        CScript opReturnScript;
        for (const auto& txout : result.standardTx->vout) {
            if (txout.scriptPubKey.size() > 0 && txout.scriptPubKey[0] == OP_RETURN) {
                opReturnScript = txout.scriptPubKey;
                break;
            }
        }

        if (!opReturnScript.empty()) {
            // Build a proper transaction with inputs via wallet coin selection
            CRecipient opReturnRecipient{CNoDestination{opReturnScript}, 0, false};

            // Get a wallet address for the shielded value output
            auto destResult = m_wallet->GetNewDestination(OutputType::BECH32, "auto_shield");
            if (destResult) {
                CRecipient shieldRecipient{*destResult, transparentBalance - 1000, true /* subtract fee */};

                std::vector<CRecipient> recipients;
                recipients.push_back(shieldRecipient);
                recipients.push_back(opReturnRecipient);

                CCoinControl coinControl;
                coinControl.m_min_depth = 1;

                auto txResult = CreateTransaction(*m_wallet, recipients, std::nullopt, coinControl, true);

                if (txResult) {
                    // Commit the properly-formed transaction
                    mapValue_t mapValue;
                    mapValue["comment"] = "Auto-shield to FCMP";
                    m_wallet->CommitTransaction(txResult->tx, std::move(mapValue), {});

                    // Register the FCMP output with the correct txid and output index
                    if (result.hasPrivKey) {
                        // Find the OP_RETURN output index in the committed TX
                        int opReturnVout = -1;
                        for (size_t vi = 0; vi < txResult->tx->vout.size(); vi++) {
                            if (txResult->tx->vout[vi].scriptPubKey.size() > 0 &&
                                txResult->tx->vout[vi].scriptPubKey[0] == OP_RETURN) {
                                opReturnVout = vi;
                                break;
                            }
                        }

                        CFcmpOutputInfo outputInfo;
                        outputInfo.outpoint = COutPoint(txResult->tx->GetHash(), opReturnVout >= 0 ? opReturnVout : 0);
                        outputInfo.amount = transparentBalance - 1000 - txResult->fee;
                        outputInfo.privKey = result.privKey;
                        outputInfo.blinding = result.blinding;
                        outputInfo.outputTuple = result.outputTuple;
                        outputInfo.keyImageHash = result.keyImageHash;
                        outputInfo.blockHeight = -1;
                        outputInfo.spent = false;
                        outputInfo.nTime = GetTime();
                        AddFcmpOutput(outputInfo);
                    }

                    LogPrintf("Auto-shield: %s WATTx shielded to FCMP (txid=%s)\n",
                              FormatMoney(transparentBalance - 1000), txResult->tx->GetHash().GetHex());
                } else {
                    LogPrintf("Auto-shield: Failed to create transaction: %s\n",
                              util::ErrorString(txResult).original);
                }
            }
        }
    }

    m_autoShieldPending = false;
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
    CPubKey ephemeralPubKey;
    CPubKey oneTimePubKey;
    curvetree::OutputTuple outputTuple = CreateOutputTuple(
        recipient, amount, blinding, privKey, ephemeralPubKey, oneTimePubKey,
        BlindingPolicy::Zero);

    // Build the transaction
    CMutableTransaction mtx;
    mtx.version = 2;

    // Add OP_RETURN output (FCMP data)
    mtx.vout.push_back(CTxOut(0, BuildNoteScript(outputTuple, ephemeralPubKey,
                                                 oneTimePubKey, amount)));

    // The wallet will add inputs and change output
    // For now, return a template that the wallet can complete

    result.standardTx = MakeTransactionRef(std::move(mtx));
    result.fee = fee;

    // Store output info in result for caller to persist after TX confirmation
    result.outputTuple = outputTuple;
    result.blinding = blinding;

    if (privKey) {
        result.hasPrivKey = true;
        result.privKey = *privKey;

        auto keyImage = GenerateKeyImage(*privKey, outputTuple.O);
        result.keyImageHash = keyImage.GetHash();

        if (m_curveTree) {
            result.leafIndex = m_curveTree->GetOutputCount();
        }
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

bool CFcmpWalletManager::UpdateFcmpOutputBlockHeight(const COutPoint& outpoint, int blockHeight)
{
    LOCK(cs_fcmp);

    auto it = m_fcmpOutputs.find(outpoint);
    if (it == m_fcmpOutputs.end()) {
        return false;
    }

    if (it->second.blockHeight != blockHeight) {
        it->second.blockHeight = blockHeight;
        WalletBatch batch(m_wallet->GetDatabase());
        batch.WriteFcmpOutput(outpoint, it->second);
        LogPrintf("FCMP: Updated output %s blockHeight to %d\n", outpoint.ToString(), blockHeight);
    }
    return true;
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

    // Persist immediately
    WalletBatch batch(m_wallet->GetDatabase());
    batch.WriteFcmpOutput(output.outpoint, output);
    if (!output.keyImageHash.IsNull()) {
        batch.WriteFcmpKeyImage(output.keyImageHash, output.outpoint);
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

    // Persist changes
    WalletBatch batch(m_wallet->GetDatabase());
    batch.WriteFcmpOutput(outpoint, it->second);
    if (!it->second.keyImageHash.IsNull()) {
        batch.WriteFcmpSpentKeyImage(it->second.keyImageHash, spendingTxHash);
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

curvetree::TreeHash CFcmpWalletManager::GetTreeRoot() const
{
    LOCK(cs_fcmp);

    if (!m_curveTree) {
        // No tree means no root. An identity/zero value here would look like a
        // real root and match nothing.
        return curvetree::TreeHash{};
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
    LOCK(cs_fcmp);

    int found = 0;
    uint256 txid = tx.GetHash();

    // Check if this is a coinbase/coinstake transaction (for maturity tracking)
    bool isCoinbaseTx = tx.IsCoinBase() || tx.IsCoinStake();

    // Get the stealth address manager to check ownership
    auto* stealthMgr = m_wallet->GetStealthAddressManager();
    if (!stealthMgr || !stealthMgr->HasStealthAddresses()) {
        return 0;
    }

    auto stealthAddresses = stealthMgr->GetStealthAddresses();

    // Look for OP_RETURN outputs with "FCMP" marker containing (O, I, C) tuples
    for (uint32_t i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];
        if (!txout.scriptPubKey.IsUnspendable()) continue;

        // Parse OP_RETURN data
        CScript::const_iterator it = txout.scriptPubKey.begin();
        opcodetype opcode;
        std::vector<unsigned char> data;

        if (!txout.scriptPubKey.GetOp(it, opcode) || opcode != OP_RETURN) continue;
        if (!txout.scriptPubKey.GetOp(it, opcode, data)) continue;

        // Check for "FCMP" marker (4 bytes) + O(32) + I(32) + C(32) = 100 bytes minimum
        if (data.size() < 100) continue;
        if (data[0] != 'F' || data[1] != 'C' || data[2] != 'M' || data[3] != 'P') continue;

        LogDebug(BCLog::PRIVACY, "FCMP scan: found FCMP marker in tx %s vout %d (data size=%zu, isCoinbase=%s)\n",
                  txid.ToString().substr(0,8), i, data.size(), isCoinbaseTx ? "true" : "false");

        // Extract O, I, C points
        ed25519::Point O, I, C;
        std::memcpy(O.data.data(), data.data() + 4, 32);
        std::memcpy(I.data.data(), data.data() + 36, 32);
        std::memcpy(C.data.data(), data.data() + 68, 32);

        // Check if ephemeral key R is appended (for stealth detection)
        CPubKey ephemeralPubKey;
        if (data.size() >= 133) {
            // R is a 33-byte compressed pubkey after O,I,C
            std::vector<unsigned char> rData(data.begin() + 100, data.begin() + 133);
            CPubKey candidate(rData);
            if (candidate.IsFullyValid()) {
                ephemeralPubKey = candidate;
            }
        }

        // Try to detect ownership via stealth address scanning
        if (!ephemeralPubKey.IsValid()) {
            continue;
        }

        LogDebug(BCLog::PRIVACY, "FCMP scan: checking %zu stealth addresses for ownership of tx %s vout %d\n",
                  stealthAddresses.size(), txid.ToString().substr(0,8), i);

        for (const auto& addrData : stealthAddresses) {
            // For FCMP outputs, bypass ScanStealthOutput (which checks view tags)
            // and use DeriveStealthSpendingKey directly. The OP_RETURN format
            // doesn't include view tags, so we verify ownership by checking that
            // the derived key produces the correct O point.
            // Output index 0, matching what the sender used.
            //
            // This passed the note's VOUT index, while GenerateStealthDestination
            // hashes with CStealthOutput::outputIndex, which is 0. They agreed
            // only for a note that happened to land at vout 0, so notes paid to
            // another wallet were simply never found.
            //
            // Zero is the right convention here rather than a bug to paper over:
            // every note carries its own ephemeral R, so the shared secret is
            // already unique per output and an index adds nothing. It exists in
            // protocols where several outputs share one R.
            CKey derivedKey;
            bool deriveResult = privacy::DeriveStealthSpendingKey(
                addrData.scanPrivKey, addrData.spendPrivKey,
                ephemeralPubKey, 0, derivedKey);

            if (!deriveResult || !derivedKey.IsValid()) {
                continue;
            }

            // Verify the derived key matches the O point in the OP_RETURN
            // O = Hash(oneTimePubKey) * G where oneTimePubKey = derivedKey.GetPubKey()
            CPubKey oneTimePubKey = derivedKey.GetPubKey();
            std::vector<uint8_t> verifyPubKeyBytes(oneTimePubKey.begin(), oneTimePubKey.end());
            uint256 verifyKeyHash = Hash(verifyPubKeyBytes);
            ed25519::Scalar verifyScalar = ed25519::Scalar::FromBytesModOrder(
                std::vector<uint8_t>(verifyKeyHash.begin(), verifyKeyHash.end()));
            ed25519::Point verifyO = verifyScalar * ed25519::Point::BasePoint();

            bool scanResult = (verifyO == O);
            LogDebug(BCLog::PRIVACY, "FCMP scan: O point match=%s for tx %s vout %d\n",
                      scanResult ? "yes" : "no", txid.ToString().substr(0,8), i);
            if (scanResult) {
                // We own this output! Create FCMP output record
                CFcmpOutputInfo outputInfo;
                outputInfo.outpoint = COutPoint(Txid::FromUint256(txid), i);
                outputInfo.outputTuple.O = O;
                outputInfo.outputTuple.I = I;
                outputInfo.outputTuple.C = C;
                outputInfo.blockHeight = blockHeight;
                outputInfo.spent = false;
                outputInfo.isCoinbaseOutput = isCoinbaseTx;
                outputInfo.nTime = GetTime();

                // Derive Ed25519 private key from the one-time public key
                // This MUST match the miner's derivation in CreateFcmpRewardOutput:
                //   O = Hash(oneTimePubKey) * G
                // The scanner derives the spending key, then gets the public key from it
                // to use the same hash input as the miner.
                CPubKey oneTimePubKey = derivedKey.GetPubKey();
                std::vector<uint8_t> pubKeyBytes(oneTimePubKey.begin(), oneTimePubKey.end());
                uint256 keyHash = Hash(pubKeyBytes);
                outputInfo.privKey = ed25519::Scalar::FromBytesModOrder(
                    std::vector<uint8_t>(keyHash.begin(), keyHash.end()));

                // Derive blinding factor deterministically from the one-time public key
                // This MUST match the miner's derivation in CreateFcmpRewardOutput
                std::vector<uint8_t> blindingInput(pubKeyBytes.begin(), pubKeyBytes.end());
                blindingInput.push_back(0x42); // domain separator for blinding derivation
                uint256 blindingHash = Hash(blindingInput);
                outputInfo.blinding = ed25519::Scalar::FromBytesModOrder(
                    std::vector<uint8_t>(blindingHash.begin(), blindingHash.end()));

                // Recover the amount from the note's encrypted field.
                //
                // txout.nValue is ZERO here -- a note is an OP_RETURN and carries
                // no value -- so reading the amount from it recorded every
                // received note as worth nothing. The value is committed to in C
                // and sent in the note under a mask only the recipient can derive.
                //
                // The recovered amount is CHECKED against the commitment rather
                // than trusted: C == v*H + b*G holds only for the right v, so a
                // wrong or corrupted field is rejected instead of producing a note
                // the wallet believes in and can never spend.
                if (data.size() >= 141) {
                    uint64_t enc = 0;
                    for (int b = 0; b < 8; ++b) {
                        enc |= static_cast<uint64_t>(data[133 + b]) << (8 * b);
                    }
                    const CAmount recovered =
                        static_cast<CAmount>(enc ^ AmountMask(oneTimePubKey));

                    if (recovered < 0 || !MoneyRange(recovered)) {
                        LogDebug(BCLog::PRIVACY, "FCMP scan: tx %s vout %d has an "
                                 "out-of-range amount; skipping\n",
                                 txid.ToString().substr(0, 8), i);
                        break;
                    }

                    const ed25519::Point expectedC =
                        ed25519::PedersenCommitment::CommitAmount(
                            static_cast<uint64_t>(recovered),
                            outputInfo.blinding).GetPoint();
                    if (!(expectedC == C)) {
                        LogDebug(BCLog::PRIVACY, "FCMP scan: tx %s vout %d amount does "
                                 "not match its commitment; skipping\n",
                                 txid.ToString().substr(0, 8), i);
                        break;
                    }
                    outputInfo.amount = recovered;
                } else {
                    LogDebug(BCLog::PRIVACY, "FCMP scan: tx %s vout %d carries no "
                             "encrypted amount; skipping\n",
                             txid.ToString().substr(0, 8), i);
                    break;
                }

                // Compute key image hash
                auto keyImage = GenerateKeyImage(outputInfo.privKey, O);
                outputInfo.keyImageHash = keyImage.GetHash();

                // The leaf index is NOT recorded here: the tree has not grown to
                // include this note yet when the wallet scans, so any value read
                // now is wrong. It is looked up at spend time by ResolveLeafIndex.

                // Add to tracked outputs
                if (AddFcmpOutput(outputInfo)) {
                    // Persist immediately
                    WalletBatch batch(m_wallet->GetDatabase());
                    batch.WriteFcmpOutput(outputInfo.outpoint, outputInfo);
                    batch.WriteFcmpKeyImage(outputInfo.keyImageHash, outputInfo.outpoint);
                    found++;
                }

                break; // One match per OP_RETURN
            }
        }
    }

    return found;
}

int CFcmpWalletManager::RollbackBlock(const CBlock& block)
{
    LOCK(cs_fcmp);

    std::set<uint256> txidsInBlock;
    for (const auto& tx : block.vtx) {
        txidsInBlock.insert(tx->GetHash());
    }

    int affected = 0;
    WalletBatch batch(m_wallet->GetDatabase());

    for (auto& [outpoint, info] : m_fcmpOutputs) {
        bool changed = false;

        // A note this block SPENT is spendable again: consensus erased the key
        // image when it disconnected the block, so refusing to reselect the note
        // would strand its value permanently.
        if (info.spent && !info.keyImageHash.IsNull()) {
            auto it = m_spentKeyImages.find(info.keyImageHash);
            if (it != m_spentKeyImages.end() && txidsInBlock.count(it->second)) {
                info.spent = false;
                m_spentKeyImages.erase(it);
                batch.EraseFcmpSpentKeyImage(info.keyImageHash);
                changed = true;
                LogPrintf("FCMP: note %s un-spent -- the transaction that spent it "
                          "was disconnected\n", outpoint.ToString());
            }
        }

        // A note this block CREATED is no longer in the curve tree, so it is not
        // spendable and must not count as confirmed. Kept rather than deleted:
        // a change note's blinding cannot be re-derived, so if the block is
        // reconnected this record is the only way to recover the note.
        if (info.blockHeight >= 0 && txidsInBlock.count(outpoint.hash)) {
            info.blockHeight = -1;
            changed = true;
            LogPrintf("FCMP: note %s marked unconfirmed -- the block that created "
                      "it was disconnected\n", outpoint.ToString());
        }

        if (changed) {
            batch.WriteFcmpOutput(outpoint, info);
            affected++;
        }
    }

    return affected;
}

int CFcmpWalletManager::ScanBlockForFcmpOutputs(
    const CBlock& block,
    int blockHeight)
{
    LOCK(cs_fcmp);

    // Update blockHeight for any outputs we already track that are in this block
    for (const auto& tx : block.vtx) {
        uint256 txid = tx->GetHash();
        for (auto& [outpoint, info] : m_fcmpOutputs) {
            if (outpoint.hash == txid && info.blockHeight < 0) {
                info.blockHeight = blockHeight;
                WalletBatch batch(m_wallet->GetDatabase());
                batch.WriteFcmpOutput(outpoint, info);
                LogPrintf("FCMP: Confirmed output %s at height %d\n", outpoint.ToString(), blockHeight);
            }
        }
    }

    // Mark notes this block SPENT.
    //
    // Spent-marking used to happen only in the sendfcmp RPC, so the wallet
    // learned about a spend only if it was the one that built it. Anything else
    // -- a spend re-mined after a reorg, a note spent by another instance of the
    // same wallet, a wallet restored from seed -- left the note looking
    // available, and coin selection handed it out again for a transaction
    // consensus was always going to reject as fcmp-keyimage-spent.
    for (const auto& tx : block.vtx) {
        privacy::CPrivacyTransaction privTx;
        if (!privacy::DecodeFcmpTransaction(*tx, privTx)) continue;
        for (const auto& input : privTx.fcmpInputs) {
            auto it = m_keyImages.find(input.keyImage.GetHash());
            if (it == m_keyImages.end()) continue;
            auto note = m_fcmpOutputs.find(it->second);
            if (note == m_fcmpOutputs.end() || note->second.spent) continue;
            MarkFcmpOutputSpent(note->first, tx->GetHash());
        }
    }

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
    LOCK(cs_fcmp);

    WalletBatch batch(m_wallet->GetDatabase(), false);

    // Load FCMP outputs via prefix cursor
    {
        DataStream prefix;
        prefix << DBKeys::FCMP_OUTPUT;
        auto cursor = batch.GetBatch().GetNewPrefixCursor(prefix);
        if (cursor) {
            DataStream ssKey, ssValue;
            while (cursor->Next(ssKey, ssValue) == DatabaseCursor::Status::MORE) {
                std::string type;
                ssKey >> type;
                Txid hash;
                uint32_t n;
                ssKey >> hash;
                ssKey >> n;

                CFcmpOutputInfo info;
                ssValue >> info;
                info.outpoint = COutPoint(hash, n);
                m_fcmpOutputs[info.outpoint] = info;

                // Rebuild key image index
                if (!info.keyImageHash.IsNull()) {
                    m_keyImages[info.keyImageHash] = info.outpoint;
                }
            }
        }
    }

    // Load spent key images
    {
        DataStream prefix;
        prefix << DBKeys::FCMP_SPENT_KI;
        auto cursor = batch.GetBatch().GetNewPrefixCursor(prefix);
        if (cursor) {
            DataStream ssKey, ssValue;
            while (cursor->Next(ssKey, ssValue) == DatabaseCursor::Status::MORE) {
                std::string type;
                ssKey >> type;
                uint256 kiHash;
                ssKey >> kiHash;

                uint256 txHash;
                ssValue >> txHash;
                m_spentKeyImages[kiHash] = txHash;
            }
        }
    }

    LogPrintf("FCMP wallet manager: loaded %d outputs, %d spent key images\n",
              m_fcmpOutputs.size(), m_spentKeyImages.size());
    return true;
}

bool CFcmpWalletManager::Save()
{
    LOCK(cs_fcmp);

    WalletBatch batch(m_wallet->GetDatabase());

    // Save all FCMP outputs
    for (const auto& [outpoint, info] : m_fcmpOutputs) {
        if (!batch.WriteFcmpOutput(outpoint, info)) {
            LogPrintf("Error: Failed to save FCMP output to DB\n");
            return false;
        }
    }

    // Save key image mappings
    for (const auto& [hash, outpoint] : m_keyImages) {
        if (!batch.WriteFcmpKeyImage(hash, outpoint)) {
            LogPrintf("Error: Failed to save FCMP key image to DB\n");
            return false;
        }
    }

    // Save spent key images
    for (const auto& [hash, txHash] : m_spentKeyImages) {
        if (!batch.WriteFcmpSpentKeyImage(hash, txHash)) {
            LogPrintf("Error: Failed to save FCMP spent key image to DB\n");
            return false;
        }
    }

    LogPrintf("FCMP wallet manager: saved %d outputs\n", m_fcmpOutputs.size());
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

CScript CFcmpWalletManager::BuildNoteScript(const curvetree::OutputTuple& tuple,
                                            const CPubKey& ephemeralPubKey,
                                            const CPubKey& oneTimePubKey,
                                            CAmount amount)
{
    std::vector<uint8_t> fcmpData;
    fcmpData.reserve(4 + 96 + 33 + 8);

    // FCMP marker "FCMP"
    fcmpData.push_back(0x46); // 'F'
    fcmpData.push_back(0x43); // 'C'
    fcmpData.push_back(0x4D); // 'M'
    fcmpData.push_back(0x50); // 'P'

    fcmpData.insert(fcmpData.end(), tuple.O.data.begin(), tuple.O.data.end());
    fcmpData.insert(fcmpData.end(), tuple.I.data.begin(), tuple.I.data.end());
    fcmpData.insert(fcmpData.end(), tuple.C.data.begin(), tuple.C.data.end());

    // R, the DKSAP ephemeral pubkey, at offset 100 -- where the scanner looks.
    // Without it the note is invisible to its recipient: ownership is decided by
    // re-deriving the one-time key from R and comparing against O, and a scan
    // that finds no valid R skips the output entirely. Notes were published
    // without it, which is why nothing but a self-shield was ever detectable.
    if (ephemeralPubKey.IsValid()) {
        fcmpData.insert(fcmpData.end(), ephemeralPubKey.begin(), ephemeralPubKey.end());

        // The encrypted amount, at offset 133.
        //
        // Without it a recipient can prove a note is theirs and recover its
        // blinding, but not learn what it is worth: the value lives only inside
        // the commitment, and recovering it from C would be a discrete log. The
        // note would be detected and still unusable.
        //
        // Recovery is self-checking -- C == v*H + b*G only holds for the right v
        // -- so a corrupted or foreign amount is detected rather than believed.
        if (oneTimePubKey.IsValid()) {
            const uint64_t enc =
                static_cast<uint64_t>(amount) ^ AmountMask(oneTimePubKey);
            for (int i = 0; i < 8; ++i) {
                fcmpData.push_back(static_cast<uint8_t>((enc >> (8 * i)) & 0xFF));
            }
        }
    }

    CScript script;
    script << OP_RETURN << fcmpData;
    return script;
}

curvetree::OutputTuple CFcmpWalletManager::CreateOutputTuple(
    const privacy::CStealthAddress& stealthAddr,
    CAmount amount,
    ed25519::Scalar& blinding,
    std::optional<ed25519::Scalar>& privKey,
    CPubKey& ephemeralPubKey,
    CPubKey& oneTimePubKey,
    BlindingPolicy blindingPolicy) const
{
    curvetree::OutputTuple tuple;

    // Generate ephemeral key for DKSAP
    CKey ephemeralKey;
    ephemeralKey.MakeNewKey(true);

    // Generate stealth destination using DKSAP protocol
    privacy::CStealthOutput stealthOut;
    const bool haveStealthOut =
        privacy::GenerateStealthDestination(stealthAddr, ephemeralKey, stealthOut);

    // R comes from the stealth output, NOT from the key made above.
    // GenerateStealthDestination draws its OWN ephemeral key and overwrites the
    // one it is handed, so reading the pubkey before the call published an R that
    // had nothing to do with the one-time key actually derived -- and the
    // recipient, re-deriving from that R, could never match O.
    ephemeralPubKey = stealthOut.ephemeral.ephemeralPubKey;
    oneTimePubKey = stealthOut.oneTimePubKey;

    if (!haveStealthOut) {
        // Fallback: use random key if stealth derivation fails
        LogPrintf("FCMP CreateOutputTuple: GenerateStealthDestination FAILED - using random key (output will NOT be spendable!)\n");
        auto kp = ed25519::KeyPair::Generate();
        tuple.O = kp.public_key;
        privKey = std::nullopt;
    } else {
        // Default: derive O from the stealth output public key (non-owned case)
        std::vector<uint8_t> pubKeyBytes(stealthOut.oneTimePubKey.begin(), stealthOut.oneTimePubKey.end());
        uint256 keyHash = Hash(pubKeyBytes);
        ed25519::Scalar outputScalar = ed25519::Scalar::FromBytesModOrder(
            std::vector<uint8_t>(keyHash.begin(), keyHash.end()));
        tuple.O = outputScalar * ed25519::Point::BasePoint();

        // Check if we own this stealth address to store private key
        auto* stealthMgr = m_wallet->GetStealthAddressManager();
        if (stealthMgr) {
            auto stealthAddresses = stealthMgr->GetStealthAddresses();
            for (const auto& addrData : stealthAddresses) {
                if (addrData.address.scanPubKey == stealthAddr.scanPubKey &&
                    addrData.address.spendPubKey == stealthAddr.spendPubKey) {
                    // We own this address - derive spending key to verify ownership
                    CKey derivedKey;
                    if (privacy::DeriveStealthSpendingKey(addrData.scanPrivKey, addrData.spendPrivKey,
                                                          ephemeralKey.GetPubKey(), 0, derivedKey)) {
                        // Derive privKey from the one-time public key (same as miner derivation)
                        // This ensures privKey * G == O (the outputScalar computed above)
                        // Both sides hash the same oneTimePubKey bytes to produce the same scalar.
                        privKey = outputScalar;
                        LogPrintf("FCMP CreateOutputTuple: derived spending key, O = privKey*G\n");
                    } else {
                        LogDebug(BCLog::PRIVACY, "FCMP CreateOutputTuple: DeriveStealthSpendingKey failed for stealth address (scanPrivKey valid=%d, spendPrivKey valid=%d)\n",
                                  addrData.scanPrivKey.IsValid(), addrData.spendPrivKey.IsValid());
                    }
                    break;
                }
            }
        }
    }

    // I = Hp(O) - key image point
    std::vector<uint8_t> toHash(tuple.O.data.begin(), tuple.O.data.end());
    tuple.I = ed25519::Point::HashToPoint(toHash);

    // C = amount*H + blinding*G. Which blinding depends on what this note is for.
    switch (blindingPolicy) {
    case BlindingPolicy::Zero:
        // A shield has no shielded inputs, so the ledger invariant reduces to
        // delta*H == sum(note commitments). delta*H carries no blinding, so the
        // note cannot carry one either or the two sides can never be equal and
        // consensus rejects every shield as fcmp-shield-imbalance.
        //
        // The cost is that the shielded AMOUNT stays visible in the tree. That
        // amount is already public from the transparent input funding the
        // shield, so nothing is revealed that was not -- but the note remains
        // linkable to it permanently. Hiding shield amounts needs a binding
        // signature proving knowledge of the blinding sum; see
        // doc/design/fcmp-value-balance.md.
        //
        // The note becomes unlinkable when it is SPENT: the pseudo-output is
        // C~ = C + r_c*G with r_c from the prover, and the membership proof
        // hides which leaf it came from.
        blinding = ed25519::Scalar::Zero();
        break;

    case BlindingPolicy::DerivedFromOneTimeKey: {
        // The recipient cannot spend a note whose blinding they cannot recover,
        // and the note format has no encrypted-amount field to send it in. So it
        // is derived from the one-time public key, which the recipient
        // reconstructs during scanning -- the same derivation, byte for byte, as
        // ScanTransactionForFcmpOutputs. Any divergence here makes every note we
        // pay out unspendable by its owner.
        if (!stealthOut.oneTimePubKey.IsValid()) {
            LogPrintf("FCMP CreateOutputTuple: no one-time key to derive a blinding "
                      "from -- output will NOT be spendable\n");
            blinding = ed25519::Scalar::Zero();
            break;
        }
        std::vector<uint8_t> blindingInput(stealthOut.oneTimePubKey.begin(),
                                           stealthOut.oneTimePubKey.end());
        blindingInput.push_back(0x42); // domain separator for blinding derivation
        const uint256 blindingHash = Hash(blindingInput);
        blinding = ed25519::Scalar::FromBytesModOrder(
            std::vector<uint8_t>(blindingHash.begin(), blindingHash.end()));
        break;
    }

    case BlindingPolicy::Explicit:
        // Caller's blinding, used as given.
        break;
    }

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

std::optional<uint64_t> CFcmpWalletManager::ResolveLeafIndexPublic(
    const CFcmpOutputInfo& output) const
{
    LOCK(cs_fcmp);
    return ResolveLeafIndex(output);
}

std::optional<uint64_t> CFcmpWalletManager::ResolveLeafIndex(
    const CFcmpOutputInfo& output) const
{
    AssertLockHeld(cs_fcmp);

    if (!m_curveTree) {
        return std::nullopt;
    }
    return m_curveTree->FindOutputIndex(output.outputTuple);
}

std::optional<privacy::CFcmpInput> CFcmpWalletManager::BuildFcmpInput(
    const CFcmpOutputInfo& output,
    const privacy::fcmp::FcmpProver::Rerandomization& rerandomized,
    const uint256& messageHash)
{
    AssertLockHeld(cs_fcmp);

    if (!m_curveTree) {
        return std::nullopt;
    }

    privacy::CFcmpInput fcmpInput;

    // Everything below comes from fcmp_prove_full, the audited prover.
    //
    // This used to hand-roll the re-randomisation, the SA+L signature and the
    // pseudo-output, then call a Schnorr-sigma scaffold for the "membership
    // proof". Three things were wrong with that:
    //
    //   * the scaffold proved nothing about C, so an attacker could present any
    //     pseudo-output beside a valid proof and value conservation was
    //     unenforceable;
    //   * C~ was computed as C + r*H -- on the VALUE generator, which changes
    //     the committed amount rather than masking it;
    //   * and the published pseudo-output was the leaf's own C, a byte-for-byte
    //     copy of a public tree leaf, which identified exactly which note was
    //     being spent and defeated the membership proof's entire purpose.
    //
    // The real prover binds C~ to the leaf it came from and produces the SA+L
    // signature inside the proof.
    //
    // The wallet's spend key is x with O = x*G and no T component, so y = 0.
    const ed25519::Scalar y = ed25519::Scalar::Zero();

    std::array<uint8_t, 32> key_image{};

    try {
        privacy::fcmp::FcmpContext ctx;
        privacy::fcmp::FcmpProver prover(m_curveTree);
        auto proofBytes = prover.GenerateFullProof(
            rerandomized, output.privKey, y, messageHash, key_image);

        fcmpInput.membershipProof = privacy::CFcmpProof(
            std::move(proofBytes),
            m_curveTree->GetRoot());
    } catch (const std::exception& e) {
        LogPrintf("FCMP: Proof generation failed: %s\n", e.what());
        return std::nullopt;
    }

    // Key image, as computed by the prover (L = x*I). Deriving it separately
    // risks the two disagreeing, which would look like a double-spend or let one
    // slip through.
    //
    // Written in the canonical CKeyImage layout -- 0x02 tag then the 32-byte
    // point -- the same encoding GenerateKeyImage produces. The bare 32 bytes
    // used to be copied over the tag, which broke two things: the wallet's
    // stored key image could never match the one that appeared on chain, so a
    // note spent in a block was never recognised as spent; and CKeyImage::IsValid
    // requires data[0] != 0, so roughly one spend in 256 was rejected as having
    // a null key image for no discoverable reason.
    fcmpInput.keyImage.data.assign(33, 0);
    fcmpInput.keyImage.data[0] = 0x02;  // Ed25519 prefix
    std::memcpy(fcmpInput.keyImage.data.data() + 1, key_image.data(), 32);

    // The pseudo-output IS the C~ this input was re-randomised to, and the same
    // one the caller balanced its output commitments against. Anything else here
    // would be a value the proof does not speak about.
    fcmpInput.pseudoOutput.data.assign(33, 0);
    fcmpInput.pseudoOutput.data[0] = 0x0E;  // ed25519 curve tag
    std::memcpy(fcmpInput.pseudoOutput.data.data() + 1, rerandomized.c_tilde.data(), 32);
    std::memcpy(fcmpInput.inputTuple.C_tilde.data.data(), rerandomized.c_tilde.data(), 32);

    // O~, I~, R and the SA+L signature live INSIDE the proof and are read from
    // it by fcmp_verify_full. Leaving hand-computed copies here would let them
    // drift from what was actually proven, so they stay unset.

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
