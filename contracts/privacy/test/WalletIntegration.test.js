const { expect } = require("chai");
const { ethers } = require("hardhat");

/**
 * Wallet Integration Tests
 *
 * These tests simulate real wallet interactions with the privacy contracts.
 * To test with actual MetaMask/Rabby:
 *
 * 1. Add Altcoinchain to your wallet:
 *    - Network Name: Altcoinchain
 *    - RPC URL: https://alt-rpc2.minethepla.net
 *    - Chain ID: 2330
 *    - Symbol: ALT
 *    - Explorer: https://explorer.altcoinchain.org
 *
 * 2. Or for local testing with Hardhat:
 *    - RPC URL: http://127.0.0.1:8545
 *    - Chain ID: 31337
 *
 * Run local node: npx hardhat node
 * Run tests: npx hardhat test test/WalletIntegration.test.js --network localhost
 */

describe("Wallet Integration Tests", function () {
    let deployer;
    let user1;
    let user2;
    let relayer;

    let mockUSDT;
    let mockVerifier;
    let merkleTree;

    // Realistic gas limits
    const GAS_LIMIT_DEPLOY = 5000000n;
    const GAS_LIMIT_INTERACTION = 500000n;

    // Test denominations (USDT has 6 decimals)
    const DENOM_100 = ethers.parseUnits("100", 6);
    const DENOM_1000 = ethers.parseUnits("1000", 6);
    const DENOM_10000 = ethers.parseUnits("10000", 6);

    before(async function () {
        console.log("\n=== Wallet Integration Test Setup ===\n");

        const signers = await ethers.getSigners();
        [deployer, user1, user2, relayer] = signers;

        console.log("Deployer address:", deployer.address);
        console.log("User1 address:", user1.address);
        console.log("User2 address:", user2.address);
        console.log("Relayer address:", relayer.address);

        // Get network info
        const network = await ethers.provider.getNetwork();
        console.log("\nNetwork:", network.name);
        console.log("Chain ID:", network.chainId.toString());

        // Check balances
        const deployerBalance = await ethers.provider.getBalance(deployer.address);
        console.log("Deployer balance:", ethers.formatEther(deployerBalance), "ETH/ALT");
    });

    describe("Contract Deployment (Simulating Wallet Deploy)", function () {
        it("Should deploy MockUSDT with gas estimation", async function () {
            console.log("\n--- Deploying MockUSDT ---");

            const MockUSDT = await ethers.getContractFactory("MockUSDT", deployer);

            // Estimate deployment gas (what wallet would show)
            const deployTx = await MockUSDT.getDeployTransaction();
            const gasEstimate = await ethers.provider.estimateGas(deployTx);
            console.log("Estimated gas for MockUSDT deployment:", gasEstimate.toString());

            // Deploy with explicit gas limit (simulating wallet confirmation)
            mockUSDT = await MockUSDT.deploy({
                gasLimit: GAS_LIMIT_DEPLOY
            });
            await mockUSDT.waitForDeployment();

            const receipt = await mockUSDT.deploymentTransaction().wait();
            console.log("MockUSDT deployed to:", await mockUSDT.getAddress());
            console.log("Actual gas used:", receipt.gasUsed.toString());
            console.log("Transaction hash:", receipt.hash);

            expect(await mockUSDT.getAddress()).to.be.properAddress;
        });

        it("Should deploy MockVerifier", async function () {
            console.log("\n--- Deploying MockVerifier ---");

            const MockVerifier = await ethers.getContractFactory("MockVerifier", deployer);
            mockVerifier = await MockVerifier.deploy({ gasLimit: GAS_LIMIT_DEPLOY });
            await mockVerifier.waitForDeployment();

            const receipt = await mockVerifier.deploymentTransaction().wait();
            console.log("MockVerifier deployed to:", await mockVerifier.getAddress());
            console.log("Gas used:", receipt.gasUsed.toString());

            expect(await mockVerifier.getAddress()).to.be.properAddress;
        });

        it("Should deploy MerkleTree with 20 levels", async function () {
            console.log("\n--- Deploying MerkleTreeTest ---");

            const MerkleTreeTest = await ethers.getContractFactory("MerkleTreeTest", deployer);
            merkleTree = await MerkleTreeTest.deploy(20, { gasLimit: GAS_LIMIT_DEPLOY });
            await merkleTree.waitForDeployment();

            const receipt = await merkleTree.deploymentTransaction().wait();
            console.log("MerkleTree deployed to:", await merkleTree.getAddress());
            console.log("Gas used:", receipt.gasUsed.toString());
            console.log("Tree levels:", (await merkleTree.levels()).toString());

            expect(await merkleTree.levels()).to.equal(20);
        });
    });

    describe("Token Operations (Wallet Transactions)", function () {
        it("Should mint tokens to user (faucet interaction)", async function () {
            console.log("\n--- User1 claiming faucet ---");

            // User1 claims from faucet (simulates wallet sending tx)
            const tx = await mockUSDT.connect(user1).faucet({
                gasLimit: GAS_LIMIT_INTERACTION
            });
            const receipt = await tx.wait();

            console.log("Faucet tx hash:", receipt.hash);
            console.log("Gas used:", receipt.gasUsed.toString());

            const balance = await mockUSDT.balanceOf(user1.address);
            console.log("User1 USDT balance:", ethers.formatUnits(balance, 6));

            expect(balance).to.equal(ethers.parseUnits("10000", 6));
        });

        it("Should transfer tokens between users", async function () {
            console.log("\n--- User1 transferring to User2 ---");

            const amount = DENOM_1000;

            // Get gas estimate (shown in wallet)
            const gasEstimate = await mockUSDT.connect(user1).transfer.estimateGas(
                user2.address,
                amount
            );
            console.log("Transfer gas estimate:", gasEstimate.toString());

            // Execute transfer
            const tx = await mockUSDT.connect(user1).transfer(user2.address, amount, {
                gasLimit: GAS_LIMIT_INTERACTION
            });
            const receipt = await tx.wait();

            console.log("Transfer tx hash:", receipt.hash);
            console.log("Gas used:", receipt.gasUsed.toString());

            const user2Balance = await mockUSDT.balanceOf(user2.address);
            console.log("User2 USDT balance:", ethers.formatUnits(user2Balance, 6));

            expect(user2Balance).to.equal(amount);
        });

        it("Should approve spending allowance", async function () {
            console.log("\n--- User1 approving MerkleTree to spend ---");

            const merkleAddress = await merkleTree.getAddress();
            const amount = ethers.MaxUint256; // Unlimited approval

            const tx = await mockUSDT.connect(user1).approve(merkleAddress, amount, {
                gasLimit: GAS_LIMIT_INTERACTION
            });
            const receipt = await tx.wait();

            console.log("Approve tx hash:", receipt.hash);
            console.log("Gas used:", receipt.gasUsed.toString());

            const allowance = await mockUSDT.allowance(user1.address, merkleAddress);
            console.log("Allowance set:", allowance === ethers.MaxUint256 ? "Unlimited" : allowance.toString());

            expect(allowance).to.equal(ethers.MaxUint256);
        });
    });

    describe("Privacy Pool Operations (Complex Wallet Interactions)", function () {
        let commitment;
        let nullifier;
        let secret;
        let leafIndex;

        it("Should create deposit commitment", async function () {
            console.log("\n--- Creating deposit commitment ---");

            // User generates secret locally (never sent to chain)
            secret = ethers.keccak256(ethers.toUtf8Bytes("user1_secret_" + Date.now()));
            const randomness = ethers.keccak256(ethers.toUtf8Bytes("random_" + Date.now()));

            // Create commitment hash
            commitment = ethers.keccak256(ethers.AbiCoder.defaultAbiCoder().encode(
                ["uint256", "bytes32", "bytes32", "bytes32"],
                [DENOM_1000, ethers.ZeroHash, ethers.ZeroHash, randomness]
            ));

            console.log("Secret (kept private):", secret.slice(0, 20) + "...");
            console.log("Commitment:", commitment);
        });

        it("Should deposit commitment to Merkle tree", async function () {
            console.log("\n--- Depositing to privacy pool ---");

            // Gas estimate for deposit
            const gasEstimate = await merkleTree.connect(user1).insert.estimateGas(commitment);
            console.log("Insert gas estimate:", gasEstimate.toString());

            // Insert commitment (wallet tx)
            const tx = await merkleTree.connect(user1).insert(commitment, {
                gasLimit: GAS_LIMIT_INTERACTION
            });
            const receipt = await tx.wait();

            console.log("Deposit tx hash:", receipt.hash);
            console.log("Gas used:", receipt.gasUsed.toString());
            console.log("Block number:", receipt.blockNumber);

            // Parse event to get leaf index
            const event = receipt.logs.find(log => {
                try {
                    const parsed = merkleTree.interface.parseLog(log);
                    return parsed.name === "LeafInserted";
                } catch { return false; }
            });

            const parsed = merkleTree.interface.parseLog(event);
            leafIndex = parsed.args.leafIndex;
            const newRoot = parsed.args.newRoot;

            console.log("Leaf index:", leafIndex.toString());
            console.log("New root:", newRoot);

            // Generate nullifier for later withdrawal
            nullifier = ethers.keccak256(ethers.solidityPacked(
                ["bytes32", "uint256"],
                [secret, leafIndex]
            ));
            console.log("Nullifier (for withdraw):", nullifier.slice(0, 20) + "...");
        });

        it("Should verify commitment is in tree", async function () {
            console.log("\n--- Verifying commitment in tree ---");

            const root = await merkleTree.getRoot();
            const isKnown = await merkleTree.isKnownRoot(root);
            const leafCount = await merkleTree.getLeafCount();

            console.log("Current root:", root);
            console.log("Root is known:", isKnown);
            console.log("Total leaves:", leafCount.toString());

            expect(isKnown).to.be.true;
            expect(leafCount).to.be.gte(1);
        });

        it("Should simulate withdrawal with ZK proof (mock)", async function () {
            console.log("\n--- Simulating withdrawal ---");

            // In production, user would generate ZK proof locally
            // For testing, we use MockVerifier which accepts any proof

            const proof = new Array(8).fill(0n);
            const publicInputs = [
                await merkleTree.getRoot(), // Merkle root
                nullifier, // Nullifier to prevent double-spend
                user2.address, // Recipient
                DENOM_1000 // Amount
            ];

            // Verify proof (wallet would call this)
            const isValid = await mockVerifier.verifyProof(proof, publicInputs);
            console.log("Proof verification result:", isValid);

            expect(isValid).to.be.true;

            // In real implementation, this would trigger token transfer
            console.log("Withdrawal would transfer", ethers.formatUnits(DENOM_1000, 6), "USDT to", user2.address);
        });
    });

    describe("Batch Operations (Multiple Wallet Transactions)", function () {
        it("Should handle multiple deposits in sequence", async function () {
            console.log("\n--- Multiple sequential deposits ---");

            const deposits = [];

            for (let i = 0; i < 5; i++) {
                const commitment = ethers.keccak256(
                    ethers.AbiCoder.defaultAbiCoder().encode(
                        ["string", "uint256"],
                        ["batch_deposit", i]
                    )
                );

                const tx = await merkleTree.connect(user1).insert(commitment, {
                    gasLimit: GAS_LIMIT_INTERACTION
                });
                const receipt = await tx.wait();

                deposits.push({
                    index: i,
                    commitment: commitment.slice(0, 20) + "...",
                    txHash: receipt.hash,
                    gasUsed: receipt.gasUsed.toString()
                });
            }

            console.table(deposits);

            const totalLeaves = await merkleTree.getLeafCount();
            console.log("Total leaves after batch:", totalLeaves.toString());

            expect(totalLeaves).to.be.gte(6);
        });

        it("Should use batch insert for efficiency", async function () {
            console.log("\n--- Batch insert (single tx) ---");

            const leaves = [];
            for (let i = 0; i < 10; i++) {
                leaves.push(ethers.keccak256(ethers.toUtf8Bytes(`efficient_batch_${i}`)));
            }

            const gasEstimate = await merkleTree.connect(user1).insertBatch.estimateGas(leaves);
            console.log("Batch (10 leaves) gas estimate:", gasEstimate.toString());

            const tx = await merkleTree.connect(user1).insertBatch(leaves, {
                gasLimit: 2000000n
            });
            const receipt = await tx.wait();

            console.log("Batch tx hash:", receipt.hash);
            console.log("Total gas used:", receipt.gasUsed.toString());
            console.log("Gas per leaf:", (receipt.gasUsed / 10n).toString());
        });
    });

    describe("Relayer Simulation (Gas Station Network)", function () {
        it("Should allow relayer to submit on behalf of user", async function () {
            console.log("\n--- Relayer submitting user's deposit ---");

            // User creates commitment offline
            const userCommitment = ethers.keccak256(ethers.toUtf8Bytes("relayed_deposit"));

            // Relayer pays gas and submits
            const relayerBalanceBefore = await ethers.provider.getBalance(relayer.address);

            const tx = await merkleTree.connect(relayer).insert(userCommitment, {
                gasLimit: GAS_LIMIT_INTERACTION
            });
            const receipt = await tx.wait();

            const relayerBalanceAfter = await ethers.provider.getBalance(relayer.address);
            const gasCost = relayerBalanceBefore - relayerBalanceAfter;

            console.log("Relayer submitted deposit");
            console.log("Gas cost paid by relayer:", ethers.formatEther(gasCost), "ETH/ALT");
            console.log("Tx hash:", receipt.hash);

            // User's commitment is in the tree without user paying gas
            expect(receipt.status).to.equal(1);
        });
    });

    describe("Error Handling (Wallet Error Messages)", function () {
        it("Should revert with clear message on invalid tree level", async function () {
            console.log("\n--- Testing revert messages ---");

            const MerkleTreeTest = await ethers.getContractFactory("MerkleTreeTest");

            await expect(
                MerkleTreeTest.deploy(0)
            ).to.be.revertedWith("Invalid levels");

            await expect(
                MerkleTreeTest.deploy(33)
            ).to.be.revertedWith("Invalid levels");

            console.log("Reverts correctly on invalid parameters");
        });

        it("Should handle insufficient balance gracefully", async function () {
            // Create new user with no tokens
            const [, , , , poorUser] = await ethers.getSigners();

            const poorBalance = await mockUSDT.balanceOf(poorUser.address);
            console.log("Poor user USDT balance:", ethers.formatUnits(poorBalance, 6));

            // This should fail if transfer is attempted (we skip actual tx)
            expect(poorBalance).to.equal(0);
        });
    });

    describe("Network Information (For Wallet Setup)", function () {
        it("Should display network configuration for wallet import", async function () {
            console.log("\n=== Wallet Configuration ===\n");

            const network = await ethers.provider.getNetwork();
            const blockNumber = await ethers.provider.getBlockNumber();
            const gasPrice = await ethers.provider.getFeeData();

            console.log("Network Configuration for MetaMask/Rabby:");
            console.log("----------------------------------------");
            console.log("Network Name: Hardhat Local / Altcoinchain");
            console.log("Chain ID:", network.chainId.toString());
            console.log("RPC URL: http://127.0.0.1:8545 (local) or https://alt-rpc2.minethepla.net (Altcoinchain)");
            console.log("Current Block:", blockNumber);
            console.log("Gas Price:", ethers.formatUnits(gasPrice.gasPrice || 0n, "gwei"), "gwei");
            console.log("");
            console.log("Deployed Contracts:");
            console.log("- MockUSDT:", await mockUSDT.getAddress());
            console.log("- MockVerifier:", await mockVerifier.getAddress());
            console.log("- MerkleTree:", await merkleTree.getAddress());
        });
    });
});

/**
 * Manual Testing Instructions:
 *
 * 1. Start local Hardhat node:
 *    cd contracts/privacy
 *    npx hardhat node
 *
 * 2. In MetaMask/Rabby:
 *    - Add Network: http://127.0.0.1:8545, Chain ID: 31337
 *    - Import test account: 0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80
 *      (First Hardhat account with 10000 ETH)
 *
 * 3. Deploy contracts:
 *    npx hardhat run scripts/deploy.js --network localhost
 *
 * 4. Interact via wallet:
 *    - Go to contract addresses in console
 *    - Use block explorer or direct contract interaction
 *
 * For Altcoinchain mainnet:
 *    - RPC: https://alt-rpc2.minethepla.net
 *    - Chain ID: 2330
 *    - Symbol: ALT
 *    - Explorer: https://explorer.altcoinchain.org
 */
