// Withdraw USDT on Polygon
require("dotenv").config();
const { ethers } = require("ethers");
const fs = require("fs");

const POLYGON_RPC = "https://polygon-bor-rpc.publicnode.com";
const BRIDGE = "0x555f7642A0420EAF329925Aad8A440b60ac4bD9D";

const BRIDGE_ABI = [
    "function withdraw(bytes calldata proof, bytes32 root, bytes32 nullifier, address recipient, uint256 amount)",
    "function nullifiers(bytes32) view returns (bool)",
    "function isValidRoot(bytes32) view returns (bool, uint256)",
    "function getLastRoot() view returns (bytes32)",
    "function getLiquidity() view returns (uint256)"
];

async function main() {
    // Get note from command line or file
    let note;
    if (process.argv[2]) {
        if (process.argv[2].startsWith("{")) {
            note = JSON.parse(process.argv[2]);
        } else {
            note = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
        }
    } else {
        console.log("Usage: node withdrawPolygon.js <note.json or JSON string> [recipient] [amount]");
        console.log("\nExample: node withdrawPolygon.js note.json 0x... 0.99");
        process.exit(1);
    }

    const recipient = process.argv[3] || process.env.RECIPIENT_ADDRESS;
    const amount = process.argv[4] || "0.99";

    if (!recipient) {
        console.log("Please provide recipient address as 3rd argument or set RECIPIENT_ADDRESS env var");
        process.exit(1);
    }

    console.log("=".repeat(60));
    console.log("POLYGON WITHDRAWAL");
    console.log("=".repeat(60));
    console.log("Recipient:", recipient);
    console.log("Amount:", amount, "USDT");

    const provider = new ethers.JsonRpcProvider(POLYGON_RPC);
    const wallet = new ethers.Wallet(process.env.DEPLOYER_PRIVATE_KEY, provider);
    const bridge = new ethers.Contract(BRIDGE, BRIDGE_ABI, wallet);

    // Check liquidity
    const liquidity = await bridge.getLiquidity();
    console.log("Pool liquidity:", ethers.formatUnits(liquidity, 6), "USDT");

    const amountWei = ethers.parseUnits(amount, 6);
    if (liquidity < amountWei) {
        console.log("ERROR: Insufficient liquidity!");
        process.exit(1);
    }

    // Get root
    const root = await bridge.getLastRoot();
    console.log("Using root:", root);

    // Check root validity
    const [isValid, sourceChain] = await bridge.isValidRoot(root);
    console.log("Root valid:", isValid, "Source chain:", sourceChain.toString());

    if (!isValid) {
        console.log("ERROR: Invalid root!");
        process.exit(1);
    }

    // Generate nullifier hash from note
    const nullifierHash = ethers.keccak256(note.nullifier);
    console.log("Nullifier hash:", nullifierHash);

    // Check if already spent
    const isSpent = await bridge.nullifiers(nullifierHash);
    if (isSpent) {
        console.log("ERROR: Note already spent!");
        process.exit(1);
    }

    // Generate dummy proof (verifier is disabled)
    const proof = ethers.hexlify(new Uint8Array(256));

    console.log("\nSending withdrawal transaction...");

    const feeData = await provider.getFeeData();
    const tx = await bridge.withdraw(
        proof,
        root,
        nullifierHash,
        recipient,
        amountWei,
        {
            gasLimit: 300000,
            maxFeePerGas: feeData.maxFeePerGas * 2n,
            maxPriorityFeePerGas: feeData.maxPriorityFeePerGas * 2n
        }
    );

    console.log("TX:", tx.hash);
    console.log("Waiting for confirmation...");

    const receipt = await tx.wait();
    console.log("\n" + "=".repeat(60));
    console.log("WITHDRAWAL SUCCESSFUL!");
    console.log("=".repeat(60));
    console.log("TX:", tx.hash);
    console.log("Block:", receipt.blockNumber);
    console.log("Recipient received:", amount, "USDT (minus 0.1% fee)");
    console.log("\nView on PolygonScan:");
    console.log(`https://polygonscan.com/tx/${tx.hash}`);
}

main().catch(e => {
    console.error("Error:", e.message);
    process.exit(1);
});
