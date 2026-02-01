// Setup Altcoinchain bridge: add liquidity and configure for testing
const { ethers } = require("hardhat");

const ALTCOINCHAIN_USDT = "0xA63C722Fc164e98cc47F05397fDab1aBb7A8f7CB";
const ALTCOINCHAIN_BRIDGE = "0x92764bADc530D9929726F86f9Af1fFE285477da4";

async function main() {
    const [deployer] = await ethers.getSigners();

    console.log("=".repeat(60));
    console.log("SETUP ALTCOINCHAIN BRIDGE");
    console.log("=".repeat(60));
    console.log("Deployer:", deployer.address);

    const usdt = await ethers.getContractAt("MockUSDT", ALTCOINCHAIN_USDT);
    const bridge = await ethers.getContractAt("PrivacyPoolBridge", ALTCOINCHAIN_BRIDGE);

    // Check current state
    const currentLiquidity = await bridge.getLiquidity();
    const currentVerifier = await bridge.verifier();
    console.log("\nCurrent state:");
    console.log("  Liquidity:", ethers.formatUnits(currentLiquidity, 6), "USDT");
    console.log("  Verifier:", currentVerifier);

    // 1. Add liquidity if needed
    if (currentLiquidity < ethers.parseUnits("1000000", 6)) {
        console.log("\n1. Adding liquidity...");
        const liquidityAmount = ethers.parseUnits("10000000", 6); // 10M USDT

        console.log("   Minting USDT...");
        await (await usdt.mint(deployer.address, liquidityAmount)).wait();

        console.log("   Approving...");
        await (await usdt.approve(ALTCOINCHAIN_BRIDGE, liquidityAmount)).wait();

        console.log("   Adding to pool...");
        await (await bridge.addLiquidity(liquidityAmount)).wait();

        const newLiquidity = await bridge.getLiquidity();
        console.log("   New liquidity:", ethers.formatUnits(newLiquidity, 6), "USDT");
    } else {
        console.log("\n1. Liquidity already sufficient");
    }

    // 2. Disable verifier for testing (allows any proof)
    // In production, keep the Groth16Verifier enabled
    if (currentVerifier !== ethers.ZeroAddress) {
        console.log("\n2. Disabling verifier for testing...");
        console.log("   (This bypasses ZK proof verification - for testing only!)");
        await (await bridge.setVerifier(ethers.ZeroAddress)).wait();
        console.log("   Verifier disabled");
    } else {
        console.log("\n2. Verifier already disabled for testing");
    }

    // Final state
    console.log("\n" + "=".repeat(60));
    console.log("SETUP COMPLETE");
    console.log("=".repeat(60));

    const finalLiquidity = await bridge.getLiquidity();
    const finalVerifier = await bridge.verifier();
    console.log("Liquidity:", ethers.formatUnits(finalLiquidity, 6), "USDT");
    console.log("Verifier:", finalVerifier === ethers.ZeroAddress ? "DISABLED (testing mode)" : finalVerifier);
    console.log("\nThe bridge is ready for anonymous transfers!");
}

main()
    .then(() => process.exit(0))
    .catch((error) => {
        console.error(error);
        process.exit(1);
    });
