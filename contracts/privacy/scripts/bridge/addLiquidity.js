// Add liquidity to PrivacyPoolBridge
const { ethers, network } = require("hardhat");

// Altcoinchain addresses
const ALTCOINCHAIN_USDT = "0xA63C722Fc164e98cc47F05397fDab1aBb7A8f7CB";
const ALTCOINCHAIN_BRIDGE = "0x92764bADc530D9929726F86f9Af1fFE285477da4";

// Sepolia addresses
const SEPOLIA_USDT = "0x44dB5E90cE123f9caf2A8078C233845ADcBBF2cB";
const SEPOLIA_BRIDGE = "0xb0E9244981998a52924aF8f203de6Bc6cAD36a7E";

async function main() {
    const [deployer] = await ethers.getSigners();
    const chainId = network.config.chainId;

    let usdtAddress, bridgeAddress;
    if (chainId === 2330) {
        usdtAddress = ALTCOINCHAIN_USDT;
        bridgeAddress = ALTCOINCHAIN_BRIDGE;
    } else if (chainId === 11155111) {
        usdtAddress = SEPOLIA_USDT;
        bridgeAddress = SEPOLIA_BRIDGE;
    } else {
        throw new Error("Unknown network");
    }

    console.log(`Adding liquidity on ${network.name} (chainId: ${chainId})`);
    console.log("Deployer:", deployer.address);

    const usdt = await ethers.getContractAt("MockUSDT", usdtAddress);
    const bridge = await ethers.getContractAt("PrivacyPoolBridge", bridgeAddress);

    // Check current liquidity
    const currentLiquidity = await bridge.getLiquidity();
    console.log("Current liquidity:", ethers.formatUnits(currentLiquidity, 6), "USDT");

    // Mint and add
    const amount = ethers.parseUnits("10000000", 6); // 10M USDT

    console.log("\nMinting...");
    await (await usdt.mint(deployer.address, amount)).wait();

    console.log("Approving...");
    await (await usdt.approve(bridgeAddress, amount)).wait();

    console.log("Adding liquidity...");
    await (await bridge.addLiquidity(amount)).wait();

    const newLiquidity = await bridge.getLiquidity();
    console.log("\nNew liquidity:", ethers.formatUnits(newLiquidity, 6), "USDT");
}

main()
    .then(() => process.exit(0))
    .catch((error) => {
        console.error(error);
        process.exit(1);
    });
