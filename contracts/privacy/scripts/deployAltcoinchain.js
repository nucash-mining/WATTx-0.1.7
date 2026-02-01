// Deploy to Altcoinchain (chainId: 2330)
const { ethers } = require("hardhat");

async function main() {
    const [deployer] = await ethers.getSigners();

    console.log("=== Deploying to Altcoinchain (chainId: 2330) ===");
    console.log("Deployer:", deployer.address);

    const balance = await ethers.provider.getBalance(deployer.address);
    console.log("Balance:", ethers.formatEther(balance), "ALT");
    console.log("");

    // 1. Deploy MockUSDT
    console.log("1. Deploying MockUSDT...");
    const MockUSDT = await ethers.getContractFactory("MockUSDT");
    const mockUSDT = await MockUSDT.deploy();
    await mockUSDT.waitForDeployment();
    const usdtAddress = await mockUSDT.getAddress();
    console.log("   MockUSDT:", usdtAddress);

    // 2. Deploy MockVerifier
    console.log("2. Deploying MockVerifier...");
    const MockVerifier = await ethers.getContractFactory("MockVerifier");
    const mockVerifier = await MockVerifier.deploy();
    await mockVerifier.waitForDeployment();
    const verifierAddress = await mockVerifier.getAddress();
    console.log("   MockVerifier:", verifierAddress);

    // 3. Deploy PrivacyPoolStandalone
    console.log("3. Deploying PrivacyPoolStandalone...");
    const PrivacyPool = await ethers.getContractFactory("PrivacyPoolStandalone");
    const privacyPool = await PrivacyPool.deploy(
        usdtAddress,
        verifierAddress,
        deployer.address
    );
    await privacyPool.waitForDeployment();
    const poolAddress = await privacyPool.getAddress();
    console.log("   PrivacyPoolStandalone:", poolAddress);

    // Summary
    console.log("");
    console.log("=== DEPLOYMENT COMPLETE ===");
    console.log("Network: Altcoinchain (chainId: 2330)");
    console.log("RPC: https://alt-rpc2.minethepla.net");
    console.log("");
    console.log("Contracts:");
    console.log("  MockUSDT:              ", usdtAddress);
    console.log("  MockVerifier:          ", verifierAddress);
    console.log("  PrivacyPoolStandalone: ", poolAddress);
    console.log("");
    console.log("Owner:", deployer.address);

    // Check remaining balance
    const remainingBalance = await ethers.provider.getBalance(deployer.address);
    console.log("Remaining balance:", ethers.formatEther(remainingBalance), "ALT");
}

main()
    .then(() => process.exit(0))
    .catch((error) => {
        console.error(error);
        process.exit(1);
    });
