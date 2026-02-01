const { ethers } = require("hardhat");

async function main() {
    const [deployer] = await ethers.getSigners();
    console.log("Deploying to Sepolia with account:", deployer.address);
    console.log("Balance:", ethers.formatEther(await ethers.provider.getBalance(deployer.address)), "ETH\n");

    // 1. Deploy MockUSDT
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

    // 3. Deploy PrivacyPoolStandalone
    console.log("\n3. Deploying PrivacyPoolStandalone...");
    const PrivacyPoolStandalone = await ethers.getContractFactory("PrivacyPoolStandalone");
    const standalone = await PrivacyPoolStandalone.deploy(
        usdtAddr,
        verifierAddr,
        deployer.address
    );
    await standalone.waitForDeployment();
    const standaloneAddr = await standalone.getAddress();
    console.log("   PrivacyPoolStandalone deployed to:", standaloneAddr);

    // 4. Deploy PrivacyPoolBridge
    console.log("\n4. Deploying PrivacyPoolBridge...");
    const PrivacyPoolBridge = await ethers.getContractFactory("PrivacyPoolBridge");
    const bridge = await PrivacyPoolBridge.deploy(
        usdtAddr,
        verifierAddr,
        deployer.address
    );
    await bridge.waitForDeployment();
    const bridgeAddr = await bridge.getAddress();
    console.log("   PrivacyPoolBridge deployed to:", bridgeAddr);

    // Summary
    console.log("\n" + "=".repeat(60));
    console.log("DEPLOYMENT SUMMARY - Sepolia Testnet");
    console.log("=".repeat(60));
    console.log("Network:              Sepolia (chainId: 11155111)");
    console.log("Deployer:             " + deployer.address);
    console.log("-".repeat(60));
    console.log("MockUSDT:             " + usdtAddr);
    console.log("MockVerifier:         " + verifierAddr);
    console.log("PrivacyPoolStandalone:" + standaloneAddr);
    console.log("PrivacyPoolBridge:    " + bridgeAddr);
    console.log("=".repeat(60));

    // Mint some test USDT to deployer
    console.log("\nMinting 1,000,000 test USDT to deployer...");
    const mintTx = await mockUSDT.mint(deployer.address, ethers.parseUnits("1000000", 6));
    await mintTx.wait();
    console.log("Done! Deployer has 1M test USDT");

    const finalBalance = await ethers.provider.getBalance(deployer.address);
    console.log("\nFinal ETH balance:", ethers.formatEther(finalBalance), "ETH");
}

main()
    .then(() => process.exit(0))
    .catch((error) => {
        console.error(error);
        process.exit(1);
    });
