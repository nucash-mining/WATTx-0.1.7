// Deposit into PrivacyPoolBridge (Sepolia)
// Generates a secret note for anonymous withdrawal on Altcoinchain
const { ethers, network } = require("hardhat");
const crypto = require("crypto");
const fs = require("fs");

// Sepolia addresses
const SEPOLIA_USDT = "0x44dB5E90cE123f9caf2A8078C233845ADcBBF2cB";
const SEPOLIA_BRIDGE = "0xb0E9244981998a52924aF8f203de6Bc6cAD36a7E";

// Generate random 31-byte values (to stay within finite field)
function randomFieldElement() {
    const bytes = crypto.randomBytes(31);
    return "0x" + bytes.toString("hex").padStart(64, "0");
}

// Generate commitment = hash(nullifier, secret)
function generateCommitment(nullifier, secret) {
    return ethers.keccak256(
        ethers.solidityPacked(["bytes32", "bytes32"], [nullifier, secret])
    );
}

// Generate nullifier hash = hash(nullifier)
function generateNullifierHash(nullifier) {
    return ethers.keccak256(ethers.solidityPacked(["bytes32"], [nullifier]));
}

async function main() {
    if (network.config.chainId !== 11155111) {
        throw new Error("This script is for Sepolia (source chain). Use --network sepolia");
    }

    const [depositor] = await ethers.getSigners();

    console.log("=".repeat(60));
    console.log("PRIVACY BRIDGE DEPOSIT (Sepolia -> Altcoinchain)");
    console.log("=".repeat(60));
    console.log("Depositor:", depositor.address);

    // Amount: 1,000,000 USDT (6 decimals)
    const amount = ethers.parseUnits("1000000", 6);
    console.log("Amount:", ethers.formatUnits(amount, 6), "USDT");
    console.log("");

    // Connect to contracts
    const usdt = await ethers.getContractAt("MockUSDT", SEPOLIA_USDT);
    const bridge = await ethers.getContractAt("PrivacyPoolBridge", SEPOLIA_BRIDGE);

    // Check balance
    let balance = await usdt.balanceOf(depositor.address);
    console.log("USDT balance:", ethers.formatUnits(balance, 6));

    if (balance < amount) {
        console.log("\nMinting MockUSDT for testing...");
        const mintTx = await usdt.mint(depositor.address, amount);
        await mintTx.wait();
        balance = await usdt.balanceOf(depositor.address);
        console.log("New balance:", ethers.formatUnits(balance, 6), "USDT");
    }

    // Generate secret note
    console.log("\n" + "=".repeat(60));
    console.log("GENERATING SECRET NOTE");
    console.log("=".repeat(60));

    const nullifier = randomFieldElement();
    const secret = randomFieldElement();
    const commitment = generateCommitment(nullifier, secret);
    const nullifierHash = generateNullifierHash(nullifier);

    console.log("Nullifier:      ", nullifier);
    console.log("Secret:         ", secret);
    console.log("Commitment:     ", commitment);
    console.log("NullifierHash:  ", nullifierHash);

    // Approve
    console.log("\nApproving USDT...");
    const approveTx = await usdt.approve(SEPOLIA_BRIDGE, amount);
    await approveTx.wait();
    console.log("Approved");

    // Deposit
    console.log("\nDepositing...");
    const depositTx = await bridge.deposit(commitment, amount);
    const receipt = await depositTx.wait();
    console.log("Deposit TX:", receipt.hash);

    // Get leaf index from event
    let leafIndex = 0n;
    for (const log of receipt.logs) {
        try {
            const parsed = bridge.interface.parseLog(log);
            if (parsed && parsed.name === "Deposit") {
                leafIndex = parsed.args.leafIndex;
            }
        } catch {}
    }
    console.log("Leaf index:", leafIndex.toString());

    // Get current Merkle root
    const merkleRoot = await bridge.getLastRoot();
    console.log("Merkle root:", merkleRoot);

    // Create the note
    const note = {
        version: 1,
        sourceChain: {
            name: "sepolia",
            chainId: 11155111,
            bridge: SEPOLIA_BRIDGE,
            token: SEPOLIA_USDT
        },
        destChain: {
            name: "altcoinchain",
            chainId: 2330,
            bridge: "0x92764bADc530D9929726F86f9Af1fFE285477da4",
            token: "0xA63C722Fc164e98cc47F05397fDab1aBb7A8f7CB"
        },
        deposit: {
            commitment,
            nullifier,
            secret,
            nullifierHash,
            amount: amount.toString(),
            leafIndex: leafIndex.toString(),
            merkleRoot,
            txHash: receipt.hash,
            timestamp: Date.now()
        }
    };

    // Save note to file
    const noteFile = `note_sepolia_${Date.now()}.json`;
    fs.writeFileSync(noteFile, JSON.stringify(note, null, 2));

    console.log("\n" + "=".repeat(60));
    console.log("DEPOSIT COMPLETE - SAVE THIS NOTE!");
    console.log("=".repeat(60));
    console.log("\n" + JSON.stringify(note, null, 2));
    console.log("\n" + "=".repeat(60));
    console.log("Note saved to:", noteFile);
    console.log("=".repeat(60));

    console.log("\nNEXT STEPS:");
    console.log("1. Run syncRoot.js to sync Merkle root to Altcoinchain");
    console.log("2. Run withdraw.js with the note file to withdraw on Altcoinchain");

    return note;
}

main()
    .then(() => process.exit(0))
    .catch((error) => {
        console.error(error);
        process.exit(1);
    });
