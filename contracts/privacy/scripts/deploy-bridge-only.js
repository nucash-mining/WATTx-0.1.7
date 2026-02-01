const { ethers } = require("hardhat");

// Already deployed on Altcoinchain
const DEPLOYED = {
    mockUSDT: "0xB538B48C1BC3A6C32e12Af29B5894B0f904f8991",
    mockVerifier: "0x9707d020C68d9A65fC4b3d3E57CA38B9BBfDAa38"
};

async function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function deployWithRetry(factory, args, maxRetries = 5) {
    for (let i = 0; i < maxRetries; i++) {
        try {
            console.log(`   Attempt ${i + 1}/${maxRetries}...`);
            const contract = await factory.deploy(...args);
            await contract.waitForDeployment();
            return contract;
        } catch (e) {
            console.log(`   Failed: ${e.message.slice(0, 100)}`);
            if (i < maxRetries - 1) {
                console.log(`   Waiting 5s before retry...`);
                await sleep(5000);
            }
        }
    }
    throw new Error("Max retries exceeded");
}

async function main() {
    const [deployer] = await ethers.getSigners();
    console.log("Deploying PrivacyPoolBridge to Altcoinchain...");
    console.log("Deployer:", deployer.address);

    const balance = await ethers.provider.getBalance(deployer.address);
    console.log("Balance:", ethers.formatEther(balance), "ALT\n");

    console.log("Using existing contracts:");
    console.log("  MockUSDT:", DEPLOYED.mockUSDT);
    console.log("  MockVerifier:", DEPLOYED.mockVerifier);

    console.log("\nDeploying PrivacyPoolBridge...");
    const PrivacyPoolBridge = await ethers.getContractFactory("PrivacyPoolBridge");
    const bridge = await deployWithRetry(PrivacyPoolBridge, [
        DEPLOYED.mockUSDT,
        DEPLOYED.mockVerifier,
        deployer.address
    ]);
    const bridgeAddr = await bridge.getAddress();
    console.log("   PrivacyPoolBridge deployed to:", bridgeAddr);

    // Mint test USDT
    console.log("\nMinting test USDT...");
    const usdt = await ethers.getContractAt("MockUSDT", DEPLOYED.mockUSDT);
    try {
        await (await usdt.mint(deployer.address, ethers.parseUnits("1000000", 6))).wait();
        console.log("   Minted 1M test USDT");
    } catch (e) {
        console.log("   Mint failed (may already have balance):", e.message.slice(0, 50));
    }

    // Summary
    console.log("\n" + "=".repeat(60));
    console.log("ALTCOINCHAIN DEPLOYMENT COMPLETE");
    console.log("=".repeat(60));
    console.log("MockUSDT:          ", DEPLOYED.mockUSDT);
    console.log("MockVerifier:      ", DEPLOYED.mockVerifier);
    console.log("PrivacyPoolBridge: ", bridgeAddr);
    console.log("=".repeat(60));

    const finalBalance = await ethers.provider.getBalance(deployer.address);
    console.log("\nFinal ALT balance:", ethers.formatEther(finalBalance), "ALT");
}

main()
    .then(() => process.exit(0))
    .catch((error) => {
        console.error(error);
        process.exit(1);
    });
