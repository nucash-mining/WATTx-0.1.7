// Sync Merkle root from Sepolia to Altcoinchain
// This acts as a simple relayer
const { ethers } = require("hardhat");
const fs = require("fs");

// Contract addresses
const SEPOLIA_BRIDGE = "0xb0E9244981998a52924aF8f203de6Bc6cAD36a7E";
const ALTCOINCHAIN_BRIDGE = "0x92764bADc530D9929726F86f9Af1fFE285477da4";

// RPC endpoints
const SEPOLIA_RPC = process.env.SEPOLIA_RPC || "https://ethereum-sepolia-rpc.publicnode.com";
const ALTCOINCHAIN_RPC = process.env.ALTCOINCHAIN_RPC || "https://alt-rpc2.minethepla.net";

async function main() {
    // Parse note file argument
    const noteFile = process.argv.find(arg => arg.endsWith('.json'));
    let targetRoot = null;

    if (noteFile && fs.existsSync(noteFile)) {
        const note = JSON.parse(fs.readFileSync(noteFile, 'utf8'));
        targetRoot = note.deposit.merkleRoot;
        console.log("Using Merkle root from note file:", noteFile);
    }

    console.log("=".repeat(60));
    console.log("MERKLE ROOT SYNC (Sepolia -> Altcoinchain)");
    console.log("=".repeat(60));

    // Connect to Sepolia to get the Merkle root
    const sepoliaProvider = new ethers.JsonRpcProvider(SEPOLIA_RPC);
    const bridgeABI = [
        "function getLastRoot() view returns (bytes32)",
        "function nextIndex() view returns (uint256)"
    ];
    const sepoliaBridge = new ethers.Contract(SEPOLIA_BRIDGE, bridgeABI, sepoliaProvider);

    const sepoliaRoot = targetRoot || await sepoliaBridge.getLastRoot();
    const sepoliaIndex = await sepoliaBridge.nextIndex();

    console.log("\nSepolia Bridge:", SEPOLIA_BRIDGE);
    console.log("Sepolia Merkle Root:", sepoliaRoot);
    console.log("Sepolia Commitment Count:", sepoliaIndex.toString());

    // Connect to Altcoinchain
    const altProvider = new ethers.JsonRpcProvider(ALTCOINCHAIN_RPC);

    // Get the relayer wallet
    const relayerKey = process.env.DEPLOYER_PRIVATE_KEY;
    if (!relayerKey) {
        throw new Error("DEPLOYER_PRIVATE_KEY not set");
    }
    const relayer = new ethers.Wallet(relayerKey, altProvider);

    console.log("\nAltcoinchain Bridge:", ALTCOINCHAIN_BRIDGE);
    console.log("Relayer:", relayer.address);

    // Check if root already exists
    const bridgeFullABI = [
        "function getLastRoot() view returns (bytes32)",
        "function isValidRoot(bytes32) view returns (bool, uint256)",
        "function addExternalRoot(bytes32, uint256)",
        "function externalRoots(bytes32) view returns (bytes32, uint256, uint256, bool)",
        "function getLiquidity() view returns (uint256)"
    ];
    const altBridge = new ethers.Contract(ALTCOINCHAIN_BRIDGE, bridgeFullABI, relayer);

    // Check liquidity
    const liquidity = await altBridge.getLiquidity();
    console.log("Altcoinchain Liquidity:", ethers.formatUnits(liquidity, 6), "USDT");

    // Check if root already synced
    const [isValid, sourceChain] = await altBridge.isValidRoot(sepoliaRoot);
    if (isValid) {
        console.log("\nRoot already synced! Source chain:", sourceChain.toString());
        return;
    }

    // Sync the root
    console.log("\nSyncing root to Altcoinchain...");
    const tx = await altBridge.addExternalRoot(sepoliaRoot, 11155111); // 11155111 = Sepolia chainId
    const receipt = await tx.wait();
    console.log("Sync TX:", receipt.hash);

    // Verify
    const [isValidNow, sourceChainNow] = await altBridge.isValidRoot(sepoliaRoot);
    console.log("\nVerification:");
    console.log("  Root valid:", isValidNow);
    console.log("  Source chain:", sourceChainNow.toString());

    console.log("\n" + "=".repeat(60));
    console.log("ROOT SYNC COMPLETE");
    console.log("=".repeat(60));
    console.log("\nThe Sepolia Merkle root is now recognized on Altcoinchain.");
    console.log("You can now withdraw anonymously using withdraw.js");
}

main()
    .then(() => process.exit(0))
    .catch((error) => {
        console.error(error);
        process.exit(1);
    });
