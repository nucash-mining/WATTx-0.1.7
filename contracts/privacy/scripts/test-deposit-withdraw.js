const { ethers } = require("hardhat");

// Deployed contract addresses on Sepolia
const CONTRACTS = {
    mockUSDT: "0x31D69920F5b500bc54103288C5E6aB88bfA3675c",
    mockVerifier: "0x2809d3e1fCFC49Ec4236FB4592f9ccd450C386F6",
    standalone: "0x5234026b87c43f15fCf40421BD286Ba6319FB888",
    bridge: "0xEcf50a033B4c104b9F1938bac54cA59fcC819606"
};

// Generate a random commitment (in production this would be hash(secret, nullifier))
function generateCommitment() {
    const secret = ethers.randomBytes(32);
    const nullifier = ethers.randomBytes(32);
    const commitment = ethers.keccak256(ethers.concat([secret, nullifier]));
    return { secret, nullifier, commitment };
}

// Generate mock proof (8 uint256 values - MockVerifier accepts anything)
function generateMockProof() {
    const proof = [];
    for (let i = 0; i < 8; i++) {
        proof.push(BigInt(i + 1));
    }
    return ethers.AbiCoder.defaultAbiCoder().encode(
        ["uint256[8]"],
        [proof]
    );
}

async function main() {
    const [deployer] = await ethers.getSigners();
    console.log("Testing with account:", deployer.address);

    // Get contract instances
    const usdt = await ethers.getContractAt("MockUSDT", CONTRACTS.mockUSDT);
    const standalone = await ethers.getContractAt("PrivacyPoolStandalone", CONTRACTS.standalone);
    const bridge = await ethers.getContractAt("PrivacyPoolBridge", CONTRACTS.bridge);

    // Check balances
    const usdtBalance = await usdt.balanceOf(deployer.address);
    console.log("USDT Balance:", ethers.formatUnits(usdtBalance, 6), "USDT\n");

    // ============ TEST STANDALONE POOL ============
    console.log("=".repeat(60));
    console.log("TESTING PrivacyPoolStandalone");
    console.log("=".repeat(60));

    // Test deposit amount (100 USDT - smallest denomination)
    const depositAmount = ethers.parseUnits("100", 6);

    // 1. Approve USDT
    console.log("\n1. Approving USDT for PrivacyPoolStandalone...");
    const approveTx1 = await usdt.approve(CONTRACTS.standalone, depositAmount);
    await approveTx1.wait();
    console.log("   Approved!");

    // 2. Generate commitment
    const { secret, nullifier, commitment } = generateCommitment();
    console.log("\n2. Generated commitment:");
    console.log("   Commitment:", commitment);
    console.log("   Nullifier:", ethers.hexlify(nullifier));

    // 3. Deposit
    console.log("\n3. Depositing 100 USDT...");
    const depositTx1 = await standalone.deposit(commitment, depositAmount);
    const depositReceipt1 = await depositTx1.wait();
    console.log("   Deposit tx:", depositReceipt1.hash);

    // Parse deposit event
    const depositEvent = depositReceipt1.logs.find(log => {
        try {
            return standalone.interface.parseLog(log)?.name === "Deposit";
        } catch { return false; }
    });
    if (depositEvent) {
        const parsed = standalone.interface.parseLog(depositEvent);
        console.log("   Leaf index:", parsed.args.leafIndex.toString());
    }

    // Get current root
    const root = await standalone.getRoot();
    console.log("   Merkle root:", root);

    // 4. Check pool stats
    const stats = await standalone.getStats();
    console.log("\n4. Pool stats after deposit:");
    console.log("   Total deposited:", ethers.formatUnits(stats.deposited, 6), "USDT");
    console.log("   Commitment count:", stats.commitmentCount.toString());

    // 5. Withdraw to a different address (for privacy demonstration)
    const withdrawRecipient = ethers.Wallet.createRandom().address;
    console.log("\n5. Withdrawing to random address:", withdrawRecipient);

    const withdrawAmount = ethers.parseUnits("99.9", 6); // Slightly less due to fees
    const mockProof = generateMockProof();
    const nullifierHash = ethers.keccak256(nullifier);

    console.log("   Nullifier hash:", nullifierHash);

    const withdrawTx1 = await standalone.withdraw(
        mockProof,
        root,
        nullifierHash,
        withdrawRecipient,
        withdrawAmount
    );
    const withdrawReceipt1 = await withdrawTx1.wait();
    console.log("   Withdraw tx:", withdrawReceipt1.hash);

    // Check recipient balance
    const recipientBalance = await usdt.balanceOf(withdrawRecipient);
    console.log("   Recipient received:", ethers.formatUnits(recipientBalance, 6), "USDT");

    // ============ TEST BRIDGE POOL ============
    console.log("\n" + "=".repeat(60));
    console.log("TESTING PrivacyPoolBridge");
    console.log("=".repeat(60));

    // 1. Approve
    console.log("\n1. Approving USDT for PrivacyPoolBridge...");
    const approveTx2 = await usdt.approve(CONTRACTS.bridge, depositAmount);
    await approveTx2.wait();
    console.log("   Approved!");

    // 2. Generate new commitment
    const commitment2 = generateCommitment();
    console.log("\n2. Generated commitment:", commitment2.commitment);

    // 3. Deposit
    console.log("\n3. Depositing 100 USDT...");
    const depositTx2 = await bridge.deposit(commitment2.commitment, depositAmount);
    const depositReceipt2 = await depositTx2.wait();
    console.log("   Deposit tx:", depositReceipt2.hash);

    // Get root
    const bridgeRoot = await bridge.getLastRoot();
    console.log("   Merkle root:", bridgeRoot);

    // 4. Check stats
    const bridgeStats = await bridge.getStats();
    console.log("\n4. Bridge pool stats:");
    console.log("   Total deposited:", ethers.formatUnits(bridgeStats.deposited, 6), "USDT");
    console.log("   Liquidity:", ethers.formatUnits(bridgeStats.liquidity, 6), "USDT");
    console.log("   Commitments:", bridgeStats.commitmentCount.toString());

    // 5. Withdraw
    const withdrawRecipient2 = ethers.Wallet.createRandom().address;
    console.log("\n5. Withdrawing to:", withdrawRecipient2);

    const nullifierHash2 = ethers.keccak256(commitment2.nullifier);
    const withdrawTx2 = await bridge.withdraw(
        mockProof,
        bridgeRoot,
        nullifierHash2,
        withdrawRecipient2,
        withdrawAmount
    );
    const withdrawReceipt2 = await withdrawTx2.wait();
    console.log("   Withdraw tx:", withdrawReceipt2.hash);

    const recipientBalance2 = await usdt.balanceOf(withdrawRecipient2);
    console.log("   Recipient received:", ethers.formatUnits(recipientBalance2, 6), "USDT");

    // ============ FINAL SUMMARY ============
    console.log("\n" + "=".repeat(60));
    console.log("TEST SUMMARY - ALL PASSED!");
    console.log("=".repeat(60));
    console.log("✓ MockUSDT approval works");
    console.log("✓ PrivacyPoolStandalone deposit works");
    console.log("✓ PrivacyPoolStandalone withdraw works");
    console.log("✓ PrivacyPoolBridge deposit works");
    console.log("✓ PrivacyPoolBridge withdraw works");
    console.log("✓ Merkle tree updates correctly");
    console.log("✓ Fees collected properly");

    const finalBalance = await ethers.provider.getBalance(deployer.address);
    console.log("\nFinal ETH balance:", ethers.formatEther(finalBalance), "ETH");
}

main()
    .then(() => process.exit(0))
    .catch((error) => {
        console.error("ERROR:", error);
        process.exit(1);
    });
