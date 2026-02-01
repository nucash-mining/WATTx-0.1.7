// Deploy PrivacyPoolBridge to any mainnet chain with real USDT
const { ethers, network } = require("hardhat");
const fs = require("fs");
const { CHAIN_CONFIG, getChainConfig } = require("./chainConfig");

async function main() {
    const [deployer] = await ethers.getSigners();
    const chainId = network.config.chainId;
    const config = getChainConfig(chainId);

    if (!config) {
        console.error("Unknown chain ID:", chainId);
        console.log("\nSupported chains:");
        Object.entries(CHAIN_CONFIG).forEach(([name, c]) => {
            console.log(`  ${name}: chainId ${c.chainId}`);
        });
        process.exit(1);
    }

    console.log("=".repeat(60));
    console.log(`DEPLOYING PRIVACY BRIDGE TO ${config.name.toUpperCase()}`);
    console.log("=".repeat(60));
    console.log("Chain ID:", chainId);
    console.log("Deployer:", deployer.address);

    const balance = await ethers.provider.getBalance(deployer.address);
    console.log("Balance:", ethers.formatEther(balance), config.symbol);
    console.log("");

    if (balance === 0n) {
        console.error("ERROR: No balance for gas. Fund the deployer wallet first.");
        process.exit(1);
    }

    // Check if USDT exists on this chain
    console.log("USDT Address:", config.usdt);
    console.log("USDT Decimals:", config.usdtDecimals);

    const usdtABI = [
        "function name() view returns (string)",
        "function symbol() view returns (string)",
        "function decimals() view returns (uint8)",
        "function balanceOf(address) view returns (uint256)"
    ];

    try {
        const usdt = new ethers.Contract(config.usdt, usdtABI, ethers.provider);
        const name = await usdt.name();
        const symbol = await usdt.symbol();
        const decimals = await usdt.decimals();
        console.log(`USDT Token: ${name} (${symbol}), ${decimals} decimals`);
    } catch (e) {
        console.error("ERROR: Cannot connect to USDT contract:", e.message);
        process.exit(1);
    }

    console.log("");

    // 1. Deploy Groth16Verifier
    console.log("1. Deploying Groth16Verifier...");
    const Groth16Verifier = await ethers.getContractFactory("Groth16Verifier");
    const verifier = await Groth16Verifier.deploy();
    await verifier.waitForDeployment();
    const verifierAddress = await verifier.getAddress();
    console.log("   Groth16Verifier:", verifierAddress);

    // 2. Deploy PrivacyPoolBridge with real USDT
    console.log("2. Deploying PrivacyPoolBridge...");
    const PrivacyPoolBridge = await ethers.getContractFactory("PrivacyPoolBridge");
    const bridge = await PrivacyPoolBridge.deploy(
        config.usdt,        // Real USDT on this chain
        verifierAddress,    // ZK verifier
        deployer.address    // Owner
    );
    await bridge.waitForDeployment();
    const bridgeAddress = await bridge.getAddress();
    console.log("   PrivacyPoolBridge:", bridgeAddress);

    // 3. Disable verifier for testing (optional - comment out for production)
    console.log("3. Disabling verifier for testing...");
    await (await bridge.setVerifier(ethers.ZeroAddress)).wait();
    console.log("   Verifier disabled (testing mode)");

    // Summary
    console.log("\n" + "=".repeat(60));
    console.log("DEPLOYMENT COMPLETE");
    console.log("=".repeat(60));
    console.log(`Network: ${config.name} (chainId: ${chainId})`);
    console.log(`Explorer: ${config.explorer}`);
    console.log("");
    console.log("Contracts:");
    console.log("  USDT (real):       ", config.usdt);
    console.log("  Groth16Verifier:   ", verifierAddress);
    console.log("  PrivacyPoolBridge: ", bridgeAddress);
    console.log("");
    console.log("Owner:", deployer.address);

    // Check liquidity (should be 0, needs real USDT deposits)
    const liquidity = await bridge.getLiquidity();
    console.log("Liquidity:", ethers.formatUnits(liquidity, config.usdtDecimals), "USDT");

    console.log("\n" + "=".repeat(60));
    console.log("IMPORTANT: LIQUIDITY REQUIRED");
    console.log("=".repeat(60));
    console.log("The bridge needs USDT liquidity for withdrawals.");
    console.log("To add liquidity:");
    console.log(`  1. Approve USDT: usdt.approve("${bridgeAddress}", amount)`);
    console.log(`  2. Add liquidity: bridge.addLiquidity(amount)`);

    // Save deployment info
    const deploymentInfo = {
        network: network.name,
        chainId,
        chainName: config.name,
        contracts: {
            usdt: config.usdt,
            usdtDecimals: config.usdtDecimals,
            Groth16Verifier: verifierAddress,
            PrivacyPoolBridge: bridgeAddress
        },
        deployer: deployer.address,
        explorer: config.explorer,
        timestamp: Date.now(),
        verifierDisabled: true // For testing
    };

    const filename = `deployment_${network.name}_mainnet.json`;
    fs.writeFileSync(filename, JSON.stringify(deploymentInfo, null, 2));
    console.log("\nSaved to:", filename);

    // Update chainConfig (print the update)
    console.log("\n" + "=".repeat(60));
    console.log("UPDATE chainConfig.js:");
    console.log("=".repeat(60));
    console.log(`    ${network.name}: {`);
    console.log(`        ...`);
    console.log(`        bridge: "${bridgeAddress}",`);
    console.log(`        verifier: "${verifierAddress}",`);
    console.log(`        ...`);
    console.log(`    }`);
}

main()
    .then(() => process.exit(0))
    .catch((error) => {
        console.error(error);
        process.exit(1);
    });
