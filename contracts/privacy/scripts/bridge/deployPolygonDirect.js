// Direct deployment to Polygon using ethers
require("dotenv").config();
const { ethers } = require("ethers");
const fs = require("fs");
const path = require("path");

const POLYGON_RPC = "https://polygon-bor-rpc.publicnode.com";
const PRIVATE_KEY = process.env.DEPLOYER_PRIVATE_KEY;
console.log("Key loaded:", PRIVATE_KEY ? "yes" : "no");
const USDT_ADDRESS = "0xc2132D05D31c914a87C6611C10748AEb04B58e8F";

async function main() {
    console.log("=".repeat(60));
    console.log("DEPLOYING PRIVACY BRIDGE TO POLYGON (Direct)");
    console.log("=".repeat(60));

    const provider = new ethers.JsonRpcProvider(POLYGON_RPC);
    const wallet = new ethers.Wallet(PRIVATE_KEY, provider);

    console.log("Deployer:", wallet.address);
    const balance = await provider.getBalance(wallet.address);
    console.log("Balance:", ethers.formatEther(balance), "MATIC");

    const feeData = await provider.getFeeData();
    console.log("Gas price:", ethers.formatUnits(feeData.gasPrice, "gwei"), "gwei");

    // Load compiled contracts
    const verifierArtifact = JSON.parse(
        fs.readFileSync(path.join(__dirname, "../../artifacts/contracts/Groth16Verifier.sol/Groth16Verifier.json"))
    );
    const bridgeArtifact = JSON.parse(
        fs.readFileSync(path.join(__dirname, "../../artifacts/contracts/PrivacyPoolBridge.sol/PrivacyPoolBridge.json"))
    );

    // Deploy Groth16Verifier
    console.log("\n1. Deploying Groth16Verifier...");
    const VerifierFactory = new ethers.ContractFactory(
        verifierArtifact.abi,
        verifierArtifact.bytecode,
        wallet
    );

    const verifier = await VerifierFactory.deploy({
        gasLimit: 5000000,
        maxFeePerGas: feeData.maxFeePerGas * 2n,
        maxPriorityFeePerGas: feeData.maxPriorityFeePerGas * 2n
    });
    console.log("   TX:", verifier.deploymentTransaction().hash);
    console.log("   Waiting for confirmation...");
    await verifier.waitForDeployment();
    const verifierAddr = await verifier.getAddress();
    console.log("   Groth16Verifier:", verifierAddr);

    // Deploy PrivacyPoolBridge
    console.log("\n2. Deploying PrivacyPoolBridge...");
    const BridgeFactory = new ethers.ContractFactory(
        bridgeArtifact.abi,
        bridgeArtifact.bytecode,
        wallet
    );

    const bridge = await BridgeFactory.deploy(
        USDT_ADDRESS,
        verifierAddr,
        wallet.address,
        {
            gasLimit: 3000000,
            maxFeePerGas: feeData.maxFeePerGas * 2n,
            maxPriorityFeePerGas: feeData.maxPriorityFeePerGas * 2n
        }
    );
    console.log("   TX:", bridge.deploymentTransaction().hash);
    console.log("   Waiting for confirmation...");
    await bridge.waitForDeployment();
    const bridgeAddr = await bridge.getAddress();
    console.log("   PrivacyPoolBridge:", bridgeAddr);

    // Disable verifier for testing
    console.log("\n3. Disabling verifier for testing...");
    const disableTx = await bridge.setVerifier(ethers.ZeroAddress, { gasLimit: 100000 });
    await disableTx.wait();
    console.log("   Done");

    // Summary
    console.log("\n" + "=".repeat(60));
    console.log("POLYGON DEPLOYMENT COMPLETE");
    console.log("=".repeat(60));
    console.log("USDT:", USDT_ADDRESS);
    console.log("Groth16Verifier:", verifierAddr);
    console.log("PrivacyPoolBridge:", bridgeAddr);

    const remaining = await provider.getBalance(wallet.address);
    console.log("Remaining:", ethers.formatEther(remaining), "MATIC");

    // Save deployment
    const deployment = {
        network: "polygon",
        chainId: 137,
        usdt: USDT_ADDRESS,
        verifier: verifierAddr,
        bridge: bridgeAddr,
        deployer: wallet.address,
        timestamp: Date.now()
    };
    fs.writeFileSync("deployment_polygon.json", JSON.stringify(deployment, null, 2));
    console.log("\nSaved to deployment_polygon.json");
}

main().catch(console.error);
