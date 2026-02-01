// Deploy to Sepolia testnet (chainId: 11155111)
const { ethers } = require("hardhat");

async function main() {
    const [deployer] = await ethers.getSigners();

    console.log("=== Deploying to Sepolia (chainId: 11155111) ===");
    console.log("Deployer:", deployer.address);

    const balance = await ethers.provider.getBalance(deployer.address);
    console.log("Balance:", ethers.formatEther(balance), "ETH");
    console.log("");

    if (balance === 0n) {
        console.log("ERROR: Wallet has no ETH. Please fund from a Sepolia faucet:");
        console.log("  - https://sepoliafaucet.com");
        console.log("  - https://www.alchemy.com/faucets/ethereum-sepolia");
        console.log("  - https://faucets.chain.link/sepolia");
        process.exit(1);
    }

    // 1. Deploy MockUSDT (test token)
    console.log("1. Deploying MockUSDT...");
    const MockUSDT = await ethers.getContractFactory("MockUSDT");
    const mockUSDT = await MockUSDT.deploy();
    await mockUSDT.waitForDeployment();
    const usdtAddress = await mockUSDT.getAddress();
    console.log("   MockUSDT:", usdtAddress);

    // 2. Deploy Groth16Verifier (real ZK verifier)
    console.log("2. Deploying Groth16Verifier...");
    const Groth16Verifier = await ethers.getContractFactory("Groth16Verifier");
    const verifier = await Groth16Verifier.deploy();
    await verifier.waitForDeployment();
    const verifierAddress = await verifier.getAddress();
    console.log("   Groth16Verifier:", verifierAddress);

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
    console.log("Network: Sepolia (chainId: 11155111)");
    console.log("");
    console.log("Contracts:");
    console.log("  MockUSDT:              ", usdtAddress);
    console.log("  Groth16Verifier:       ", verifierAddress);
    console.log("  PrivacyPoolStandalone: ", poolAddress);
    console.log("");
    console.log("Owner:", deployer.address);

    // Check remaining balance
    const remainingBalance = await ethers.provider.getBalance(deployer.address);
    console.log("Remaining balance:", ethers.formatEther(remainingBalance), "ETH");

    // Etherscan verification commands
    console.log("");
    console.log("=== VERIFY ON ETHERSCAN ===");
    console.log("npx hardhat verify --network sepolia", usdtAddress);
    console.log("npx hardhat verify --network sepolia", verifierAddress);
    console.log("npx hardhat verify --network sepolia", poolAddress, usdtAddress, verifierAddress, deployer.address);
}

main()
    .then(() => process.exit(0))
    .catch((error) => {
        console.error(error);
        process.exit(1);
    });
