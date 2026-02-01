// SPDX-License-Identifier: MIT
// Deployment script for WATTx Privacy Pool system

const { ethers } = require("hardhat");

/**
 * Deployment addresses by network
 */
const ADDRESSES = {
    // WATTx EVM (main privacy controller)
    wattx: {
        lzEndpoint: "", // LayerZero endpoint - to be deployed
        chainId: 30001, // WATTx LayerZero chain ID (placeholder)
    },
    // Altcoinchain EVM (chain ID 2330)
    altcoinchain: {
        lzEndpoint: "", // LayerZero endpoint - to be deployed or use custom bridge
        usdt: "", // USDT contract on Altcoinchain - deploy or use bridged
        chainId: 2330, // Altcoinchain native chain ID
        lzChainId: 32330, // LayerZero EID for Altcoinchain (custom, needs registration)
    },
    // Ethereum Mainnet
    ethereum: {
        lzEndpoint: "0x1a44076050125825900e736c501f859c50fE728c",
        usdt: "0xdAC17F958D2ee523a2206206994597C13D831ec7",
        chainId: 30101, // Ethereum LayerZero v2 EID
    },
    // BSC
    bsc: {
        lzEndpoint: "0x1a44076050125825900e736c501f859c50fE728c",
        usdt: "0x55d398326f99059fF775485246999027B3197955", // BSC-USD
        chainId: 30102, // BSC LayerZero v2 EID
    },
    // Polygon
    polygon: {
        lzEndpoint: "0x1a44076050125825900e736c501f859c50fE728c",
        usdt: "0xc2132D05D31c914a87C6611C10748AEb04B58e8F",
        chainId: 30109, // Polygon LayerZero v2 EID
    },
    // Arbitrum
    arbitrum: {
        lzEndpoint: "0x1a44076050125825900e736c501f859c50fE728c",
        usdt: "0xFd086bC7CD5C481DCC9C85ebE478A1C0b69FCbb9",
        chainId: 30110, // Arbitrum LayerZero v2 EID
    },
    // Avalanche
    avalanche: {
        lzEndpoint: "0x1a44076050125825900e736c501f859c50fE728c",
        usdt: "0x9702230A8Ea53601f5cD2dc00fDBc13d4dF4A8c7",
        chainId: 30106, // Avalanche LayerZero v2 EID
    },
    // Sepolia (testnet)
    sepolia: {
        lzEndpoint: "0x6EDCE65403992e310A62460808c4b910D972f10f",
        usdt: "", // Deploy mock USDT for testing
        chainId: 40161, // Sepolia LayerZero v2 EID
    },
};

/**
 * Deploy MockVerifier for testing
 */
async function deployMockVerifier() {
    console.log("Deploying MockVerifier...");
    const MockVerifier = await ethers.getContractFactory("MockVerifier");
    const verifier = await MockVerifier.deploy();
    await verifier.waitForDeployment();
    console.log("MockVerifier deployed to:", await verifier.getAddress());
    return verifier;
}

/**
 * Deploy PrivacyController on WATTx EVM
 */
async function deployPrivacyController(lzEndpoint, verifierAddress, owner) {
    console.log("Deploying PrivacyController...");
    const PrivacyController = await ethers.getContractFactory("PrivacyController");
    const controller = await PrivacyController.deploy(lzEndpoint, verifierAddress, owner);
    await controller.waitForDeployment();
    console.log("PrivacyController deployed to:", await controller.getAddress());
    return controller;
}

/**
 * Deploy PrivacyPool on external chain
 */
async function deployPrivacyPool(usdtAddress, lzEndpoint, wattxChainId, owner) {
    console.log("Deploying PrivacyPool...");
    const PrivacyPool = await ethers.getContractFactory("PrivacyPool");
    const pool = await PrivacyPool.deploy(usdtAddress, lzEndpoint, wattxChainId, owner);
    await pool.waitForDeployment();
    console.log("PrivacyPool deployed to:", await pool.getAddress());
    return pool;
}

/**
 * Deploy mock USDT for testing
 */
async function deployMockUSDT() {
    console.log("Deploying MockUSDT...");
    const MockUSDT = await ethers.getContractFactory("MockUSDT");
    const usdt = await MockUSDT.deploy();
    await usdt.waitForDeployment();
    console.log("MockUSDT deployed to:", await usdt.getAddress());
    return usdt;
}

/**
 * Configure chain connections after deployment
 */
async function configureChains(controller, poolAddresses) {
    console.log("Configuring chain connections...");

    for (const [chainName, { chainId, poolAddress, initialLiquidity }] of Object.entries(poolAddresses)) {
        console.log(`  Configuring ${chainName} (chainId: ${chainId})...`);
        const tx = await controller.configureChain(chainId, poolAddress, initialLiquidity || 0);
        await tx.wait();
        console.log(`  ${chainName} configured`);
    }
}

/**
 * Main deployment function for testnet
 */
async function deployTestnet() {
    const [deployer] = await ethers.getSigners();
    console.log("Deploying with account:", deployer.address);
    console.log("Account balance:", ethers.formatEther(await ethers.provider.getBalance(deployer.address)));

    // Step 1: Deploy mock USDT
    const mockUSDT = await deployMockUSDT();

    // Step 2: Deploy mock verifier
    const verifier = await deployMockVerifier();

    // Step 3: Deploy PrivacyController (on WATTx testnet)
    const lzEndpoint = ADDRESSES.sepolia.lzEndpoint; // Use Sepolia for testing
    const controller = await deployPrivacyController(
        lzEndpoint,
        await verifier.getAddress(),
        deployer.address
    );

    // Step 4: Deploy PrivacyPool (on Sepolia, simulating external chain)
    const pool = await deployPrivacyPool(
        await mockUSDT.getAddress(),
        lzEndpoint,
        ADDRESSES.wattx.chainId,
        deployer.address
    );

    // Step 5: Configure trusted remotes (simplified for testnet)
    console.log("\n=== Deployment Summary ===");
    console.log("MockUSDT:", await mockUSDT.getAddress());
    console.log("MockVerifier:", await verifier.getAddress());
    console.log("PrivacyController:", await controller.getAddress());
    console.log("PrivacyPool:", await pool.getAddress());

    return { mockUSDT, verifier, controller, pool };
}

/**
 * Main deployment function for mainnet
 */
async function deployMainnet(networkName) {
    const network = ADDRESSES[networkName];
    if (!network) {
        throw new Error(`Unknown network: ${networkName}`);
    }

    const [deployer] = await ethers.getSigners();
    console.log(`Deploying to ${networkName} with account:`, deployer.address);

    // For mainnet, use real verifier (generated by circom)
    // This would be the output of: snarkjs zkey export solidityverifier
    throw new Error("Real verifier contract required for mainnet deployment");
}

/**
 * Deploy to Altcoinchain EVM (chain ID 2330)
 *
 * Since Altcoinchain may not have LayerZero support yet,
 * this deploys with a custom bridge endpoint placeholder.
 */
async function deployAltcoinchain() {
    const [deployer] = await ethers.getSigners();
    console.log("Deploying to Altcoinchain (chainId: 2330) with account:", deployer.address);
    console.log("Account balance:", ethers.formatEther(await ethers.provider.getBalance(deployer.address)));

    // Step 1: Deploy mock USDT (or use existing bridged USDT)
    let usdtAddress = ADDRESSES.altcoinchain.usdt;
    let mockUSDT;

    if (!usdtAddress) {
        console.log("No USDT address configured, deploying MockUSDT...");
        mockUSDT = await deployMockUSDT();
        usdtAddress = await mockUSDT.getAddress();
    }

    // Step 2: Deploy mock verifier
    const verifier = await deployMockVerifier();

    // Step 3: For Altcoinchain, we have two options:
    // A) Deploy as PrivacyController (if this is the privacy hub)
    // B) Deploy as PrivacyPool (if WATTx is the privacy hub)

    // Deploying as PrivacyPool - Altcoinchain connects to WATTx
    console.log("\nDeploying PrivacyPool on Altcoinchain...");
    console.log("Note: LayerZero endpoint needs to be deployed or use custom bridge");

    // Use a placeholder endpoint - will need to be updated
    const lzEndpointPlaceholder = deployer.address; // Placeholder - replace with real endpoint

    const pool = await deployPrivacyPool(
        usdtAddress,
        lzEndpointPlaceholder, // Replace with LayerZero endpoint when available
        ADDRESSES.wattx.chainId,
        deployer.address
    );

    console.log("\n=== Altcoinchain Deployment Summary ===");
    console.log("Network: Altcoinchain (chainId: 2330)");
    console.log("USDT:", usdtAddress);
    console.log("MockVerifier:", await verifier.getAddress());
    console.log("PrivacyPool:", await pool.getAddress());
    console.log("\n⚠️  NOTE: LayerZero endpoint is a placeholder.");
    console.log("    Update with real endpoint address for cross-chain messaging.");

    return { usdtAddress, verifier, pool };
}

/**
 * Deploy full system across multiple chains
 */
async function deployMultiChain(networks) {
    const deployments = {};

    for (const network of networks) {
        console.log(`\n========== Deploying to ${network} ==========\n`);

        if (network === "altcoinchain") {
            deployments[network] = await deployAltcoinchain();
        } else if (network === "wattx") {
            // WATTx gets the PrivacyController
            deployments[network] = await deployTestnet(); // Modify for mainnet
        } else {
            // Other chains get PrivacyPool
            deployments[network] = await deployMainnet(network);
        }
    }

    return deployments;
}

// Export for Hardhat
module.exports = {
    deployTestnet,
    deployMainnet,
    deployAltcoinchain,
    deployMultiChain,
    ADDRESSES
};

// Direct execution
if (require.main === module) {
    deployTestnet()
        .then(() => process.exit(0))
        .catch((error) => {
            console.error(error);
            process.exit(1);
        });
}
