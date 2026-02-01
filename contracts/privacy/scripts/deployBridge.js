// Deploy PrivacyPoolBridge with cross-chain support
const { ethers, network } = require("hardhat");

async function main() {
    const [deployer] = await ethers.getSigners();
    const chainId = network.config.chainId;

    console.log(`=== Deploying PrivacyPoolBridge to ${network.name} (chainId: ${chainId}) ===`);
    console.log("Deployer:", deployer.address);

    const balance = await ethers.provider.getBalance(deployer.address);
    console.log("Balance:", ethers.formatEther(balance), chainId === 2330 ? "ALT" : "ETH");
    console.log("");

    // 1. Deploy or use existing MockUSDT
    console.log("1. Deploying MockUSDT...");
    const MockUSDT = await ethers.getContractFactory("MockUSDT");
    const mockUSDT = await MockUSDT.deploy();
    await mockUSDT.waitForDeployment();
    const usdtAddress = await mockUSDT.getAddress();
    console.log("   MockUSDT:", usdtAddress);

    // 2. Deploy Groth16Verifier
    console.log("2. Deploying Groth16Verifier...");
    const Groth16Verifier = await ethers.getContractFactory("Groth16Verifier");
    const verifier = await Groth16Verifier.deploy();
    await verifier.waitForDeployment();
    const verifierAddress = await verifier.getAddress();
    console.log("   Groth16Verifier:", verifierAddress);

    // 3. Deploy PrivacyPoolBridge
    console.log("3. Deploying PrivacyPoolBridge...");
    const PrivacyPoolBridge = await ethers.getContractFactory("PrivacyPoolBridge");
    const bridge = await PrivacyPoolBridge.deploy(
        usdtAddress,
        verifierAddress,
        deployer.address
    );
    await bridge.waitForDeployment();
    const bridgeAddress = await bridge.getAddress();
    console.log("   PrivacyPoolBridge:", bridgeAddress);

    // 4. Mint initial liquidity
    console.log("\n4. Adding initial liquidity...");
    const liquidityAmount = ethers.parseUnits("10000000", 6); // 10M USDT
    await (await mockUSDT.mint(deployer.address, liquidityAmount)).wait();
    await (await mockUSDT.approve(bridgeAddress, liquidityAmount)).wait();
    await (await bridge.addLiquidity(liquidityAmount)).wait();
    console.log("   Added 10,000,000 USDT liquidity");

    // Summary
    console.log("\n" + "=".repeat(60));
    console.log("DEPLOYMENT COMPLETE");
    console.log("=".repeat(60));
    console.log(`Network: ${network.name} (chainId: ${chainId})`);
    console.log("");
    console.log("Contracts:");
    console.log("  MockUSDT:          ", usdtAddress);
    console.log("  Groth16Verifier:   ", verifierAddress);
    console.log("  PrivacyPoolBridge: ", bridgeAddress);
    console.log("");
    console.log("Owner/Relayer:", deployer.address);

    const liquidity = await bridge.getLiquidity();
    console.log("Liquidity:", ethers.formatUnits(liquidity, 6), "USDT");

    // Save deployment info
    const fs = require("fs");
    const deploymentInfo = {
        network: network.name,
        chainId,
        contracts: {
            MockUSDT: usdtAddress,
            Groth16Verifier: verifierAddress,
            PrivacyPoolBridge: bridgeAddress
        },
        deployer: deployer.address,
        timestamp: Date.now()
    };

    const filename = `deployment_${network.name}.json`;
    fs.writeFileSync(filename, JSON.stringify(deploymentInfo, null, 2));
    console.log("\nSaved to:", filename);
}

main()
    .then(() => process.exit(0))
    .catch((error) => {
        console.error(error);
        process.exit(1);
    });
