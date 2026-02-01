// Deploy Groth16Verifier to Altcoinchain and update PrivacyPool
const { ethers } = require("hardhat");

const PRIVACY_POOL_ADDRESS = "0x14b6Bd4341238cBF0Ab3C734790eBbADB9eb4b4E";

async function main() {
    const [deployer] = await ethers.getSigners();

    console.log("=== Deploying Groth16Verifier to Altcoinchain ===");
    console.log("Deployer:", deployer.address);

    const balance = await ethers.provider.getBalance(deployer.address);
    console.log("Balance:", ethers.formatEther(balance), "ALT");
    console.log("");

    // 1. Deploy Groth16Verifier
    console.log("1. Deploying Groth16Verifier...");
    const Groth16Verifier = await ethers.getContractFactory("Groth16Verifier");
    const verifier = await Groth16Verifier.deploy();
    await verifier.waitForDeployment();
    const verifierAddress = await verifier.getAddress();
    console.log("   Groth16Verifier:", verifierAddress);

    // 2. Update PrivacyPool to use new verifier
    console.log("");
    console.log("2. Updating PrivacyPool verifier...");
    const PrivacyPool = await ethers.getContractFactory("PrivacyPoolStandalone");
    const pool = PrivacyPool.attach(PRIVACY_POOL_ADDRESS);

    const tx = await pool.setVerifier(verifierAddress);
    await tx.wait();
    console.log("   PrivacyPool updated to use Groth16Verifier");

    // 3. Verify the update
    console.log("");
    console.log("3. Verifying configuration...");
    const currentVerifier = await pool.verifier();
    console.log("   Pool verifier:", currentVerifier);
    console.log("   Match:", currentVerifier.toLowerCase() === verifierAddress.toLowerCase() ? "✅" : "❌");

    // Summary
    console.log("");
    console.log("=== DEPLOYMENT COMPLETE ===");
    console.log("Network: Altcoinchain (chainId: 2330)");
    console.log("");
    console.log("Contracts:");
    console.log("  Groth16Verifier:       ", verifierAddress);
    console.log("  PrivacyPoolStandalone: ", PRIVACY_POOL_ADDRESS);
    console.log("");
    console.log("The privacy pool now uses real ZK proof verification!");

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
