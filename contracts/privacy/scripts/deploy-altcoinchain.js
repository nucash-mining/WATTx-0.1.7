const { ethers } = require("hardhat");

async function main() {
    const [deployer] = await ethers.getSigners();
    console.log("Deploying to Altcoinchain with account:", deployer.address);

    const balance = await ethers.provider.getBalance(deployer.address);
    console.log("Balance:", ethers.formatEther(balance), "ALT\n");

    if (balance === 0n) {
        console.log("ERROR: No ALT balance. Please fund the deployer address.");
        console.log("Deployer address:", deployer.address);
        process.exit(1);
    }

    // 1. Deploy MockUSDT (for testing on Altcoinchain)
    console.log("1. Deploying MockUSDT...");
    const MockUSDT = await ethers.getContractFactory("MockUSDT");
    const mockUSDT = await MockUSDT.deploy();
    await mockUSDT.waitForDeployment();
    const usdtAddr = await mockUSDT.getAddress();
    console.log("   MockUSDT deployed to:", usdtAddr);

    // 2. Deploy MockVerifier
    console.log("\n2. Deploying MockVerifier...");
    const MockVerifier = await ethers.getContractFactory("MockVerifier");
    const verifier = await MockVerifier.deploy();
    await verifier.waitForDeployment();
    const verifierAddr = await verifier.getAddress();
    console.log("   MockVerifier deployed to:", verifierAddr);

    // 3. Deploy PrivacyPoolBridge (for cross-chain)
    console.log("\n3. Deploying PrivacyPoolBridge...");
    const PrivacyPoolBridge = await ethers.getContractFactory("PrivacyPoolBridge");
    const bridge = await PrivacyPoolBridge.deploy(
        usdtAddr,
        verifierAddr,
        deployer.address
    );
    await bridge.waitForDeployment();
    const bridgeAddr = await bridge.getAddress();
    console.log("   PrivacyPoolBridge deployed to:", bridgeAddr);

    // 4. Mint test USDT
    console.log("\n4. Minting 1,000,000 test USDT...");
    await (await mockUSDT.mint(deployer.address, ethers.parseUnits("1000000", 6))).wait();
    console.log("   Done!");

    // Summary
    console.log("\n" + "=".repeat(60));
    console.log("DEPLOYMENT SUMMARY - Altcoinchain");
    console.log("=".repeat(60));
    console.log("Network:              Altcoinchain (chainId: 2330)");
    console.log("Deployer:             " + deployer.address);
    console.log("-".repeat(60));
    console.log("MockUSDT:             " + usdtAddr);
    console.log("MockVerifier:         " + verifierAddr);
    console.log("PrivacyPoolBridge:    " + bridgeAddr);
    console.log("=".repeat(60));

    // Save addresses for cross-chain testing
    console.log("\n// Add to ALTCOINCHAIN_CONTRACTS:");
    console.log(`const ALTCOINCHAIN_CONTRACTS = {`);
    console.log(`    mockUSDT: "${usdtAddr}",`);
    console.log(`    mockVerifier: "${verifierAddr}",`);
    console.log(`    bridge: "${bridgeAddr}"`);
    console.log(`};`);

    const finalBalance = await ethers.provider.getBalance(deployer.address);
    console.log("\nFinal ALT balance:", ethers.formatEther(finalBalance), "ALT");
}

main()
    .then(() => process.exit(0))
    .catch((error) => {
        console.error(error);
        process.exit(1);
    });
