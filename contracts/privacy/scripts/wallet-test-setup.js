/**
 * Wallet Test Setup Script
 *
 * This script deploys contracts and outputs configuration for MetaMask/Rabby testing.
 *
 * Usage:
 *   1. Start local node: npx hardhat node
 *   2. In another terminal: npx hardhat run scripts/wallet-test-setup.js --network localhost
 *   3. Configure your wallet with the displayed settings
 */

const hre = require("hardhat");

async function main() {
    console.log("\n╔══════════════════════════════════════════════════════════════╗");
    console.log("║         WATTx Privacy - Wallet Integration Setup             ║");
    console.log("╚══════════════════════════════════════════════════════════════╝\n");

    const [deployer, user1, user2] = await hre.ethers.getSigners();
    const network = await hre.ethers.provider.getNetwork();

    console.log("Network:", network.name);
    console.log("Chain ID:", network.chainId.toString());
    console.log("Deployer:", deployer.address);
    console.log("");

    // Deploy contracts
    console.log("📦 Deploying contracts...\n");

    // MockUSDT
    const MockUSDT = await hre.ethers.getContractFactory("MockUSDT");
    const mockUSDT = await MockUSDT.deploy();
    await mockUSDT.waitForDeployment();
    const mockUSDTAddress = await mockUSDT.getAddress();
    console.log("✓ MockUSDT deployed to:", mockUSDTAddress);

    // MockVerifier
    const MockVerifier = await hre.ethers.getContractFactory("MockVerifier");
    const mockVerifier = await MockVerifier.deploy();
    await mockVerifier.waitForDeployment();
    const mockVerifierAddress = await mockVerifier.getAddress();
    console.log("✓ MockVerifier deployed to:", mockVerifierAddress);

    // MerkleTree
    const MerkleTreeTest = await hre.ethers.getContractFactory("MerkleTreeTest");
    const merkleTree = await MerkleTreeTest.deploy(20);
    await merkleTree.waitForDeployment();
    const merkleTreeAddress = await merkleTree.getAddress();
    console.log("✓ MerkleTree deployed to:", merkleTreeAddress);

    // Mint some tokens to test accounts
    console.log("\n💰 Minting test tokens...\n");
    await mockUSDT.mint(user1.address, hre.ethers.parseUnits("100000", 6));
    await mockUSDT.mint(user2.address, hre.ethers.parseUnits("50000", 6));
    console.log("✓ Minted 100,000 USDT to", user1.address);
    console.log("✓ Minted 50,000 USDT to", user2.address);

    // Output wallet configuration
    console.log("\n");
    console.log("╔══════════════════════════════════════════════════════════════╗");
    console.log("║                    WALLET CONFIGURATION                       ║");
    console.log("╚══════════════════════════════════════════════════════════════╝\n");

    console.log("┌─────────────────────────────────────────────────────────────┐");
    console.log("│ Add Network to MetaMask/Rabby:                              │");
    console.log("├─────────────────────────────────────────────────────────────┤");
    console.log("│ Network Name: Hardhat Local                                 │");
    console.log("│ RPC URL:      http://127.0.0.1:8545                         │");
    console.log("│ Chain ID:     31337                                         │");
    console.log("│ Symbol:       ETH                                           │");
    console.log("└─────────────────────────────────────────────────────────────┘\n");

    console.log("┌─────────────────────────────────────────────────────────────┐");
    console.log("│ Import Test Accounts (Private Keys):                        │");
    console.log("├─────────────────────────────────────────────────────────────┤");
    console.log("│ Account #0 (Deployer - 10000 ETH):                          │");
    console.log("│ 0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80");
    console.log("│                                                             │");
    console.log("│ Account #1 (User1 - 10000 ETH + 100k USDT):                 │");
    console.log("│ 0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d");
    console.log("│                                                             │");
    console.log("│ Account #2 (User2 - 10000 ETH + 50k USDT):                  │");
    console.log("│ 0x5de4111afa1a4b94908f83103eb1f1706367c2e68ca870fc3fb9a804cdab365a");
    console.log("└─────────────────────────────────────────────────────────────┘\n");

    console.log("┌─────────────────────────────────────────────────────────────┐");
    console.log("│ Add Custom Token (MockUSDT):                                │");
    console.log("├─────────────────────────────────────────────────────────────┤");
    console.log(`│ Token Address: ${mockUSDTAddress}│`);
    console.log("│ Token Symbol:  USDT                                         │");
    console.log("│ Decimals:      6                                            │");
    console.log("└─────────────────────────────────────────────────────────────┘\n");

    console.log("┌─────────────────────────────────────────────────────────────┐");
    console.log("│ Deployed Contract Addresses:                                │");
    console.log("├─────────────────────────────────────────────────────────────┤");
    console.log(`│ MockUSDT:     ${mockUSDTAddress}│`);
    console.log(`│ MockVerifier: ${mockVerifierAddress}│`);
    console.log(`│ MerkleTree:   ${merkleTreeAddress}│`);
    console.log("└─────────────────────────────────────────────────────────────┘\n");

    // Test interactions
    console.log("╔══════════════════════════════════════════════════════════════╗");
    console.log("║                    TEST INTERACTIONS                          ║");
    console.log("╚══════════════════════════════════════════════════════════════╝\n");

    console.log("Test 1: USDT Faucet (get 10,000 USDT)");
    console.log("  Contract: " + mockUSDTAddress);
    console.log("  Function: faucet()");
    console.log("  Data:     0x7b56c2b2");
    console.log("");

    console.log("Test 2: Insert Commitment to Privacy Pool");
    console.log("  Contract: " + merkleTreeAddress);
    console.log("  Function: insert(bytes32 leaf)");
    console.log("  Example leaf: 0x" + "a".repeat(64));
    console.log("");

    console.log("Test 3: Check USDT Balance");
    console.log("  Contract: " + mockUSDTAddress);
    console.log("  Function: balanceOf(address)");
    console.log("  View call - no gas needed");
    console.log("");

    // For Altcoinchain mainnet
    console.log("╔══════════════════════════════════════════════════════════════╗");
    console.log("║               ALTCOINCHAIN MAINNET CONFIG                     ║");
    console.log("╚══════════════════════════════════════════════════════════════╝\n");

    console.log("┌─────────────────────────────────────────────────────────────┐");
    console.log("│ Network Name: Altcoinchain                                  │");
    console.log("│ RPC URL:      https://alt-rpc2.minethepla.net               │");
    console.log("│ Chain ID:     2330                                          │");
    console.log("│ Symbol:       ALT                                           │");
    console.log("│ Explorer:     https://explorer.altcoinchain.org             │");
    console.log("└─────────────────────────────────────────────────────────────┘\n");

    console.log("🎉 Setup complete! You can now interact with contracts via your wallet.\n");

    // Return addresses for scripting
    return {
        mockUSDT: mockUSDTAddress,
        mockVerifier: mockVerifierAddress,
        merkleTree: merkleTreeAddress,
        chainId: network.chainId.toString()
    };
}

main()
    .then((addresses) => {
        console.log("Exported addresses:", JSON.stringify(addresses, null, 2));
        process.exit(0);
    })
    .catch((error) => {
        console.error(error);
        process.exit(1);
    });
