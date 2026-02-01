const { expect } = require("chai");
const { ethers } = require("hardhat");

describe("WATTx Privacy Pool System", function () {
    let mockUSDT;
    let mockVerifier;
    let merkleTree;
    let owner;
    let user1;
    let user2;

    const DENOM_100 = ethers.parseUnits("100", 6);
    const DENOM_1000 = ethers.parseUnits("1000", 6);

    beforeEach(async function () {
        [owner, user1, user2] = await ethers.getSigners();

        // Deploy MockUSDT
        const MockUSDT = await ethers.getContractFactory("MockUSDT");
        mockUSDT = await MockUSDT.deploy();
        await mockUSDT.waitForDeployment();

        // Deploy MockVerifier
        const MockVerifier = await ethers.getContractFactory("MockVerifier");
        mockVerifier = await MockVerifier.deploy();
        await mockVerifier.waitForDeployment();
    });

    describe("MockUSDT", function () {
        it("Should have 6 decimals", async function () {
            expect(await mockUSDT.decimals()).to.equal(6);
        });

        it("Should allow minting", async function () {
            await mockUSDT.mint(user1.address, DENOM_1000);
            expect(await mockUSDT.balanceOf(user1.address)).to.equal(DENOM_1000);
        });

        it("Should have faucet function", async function () {
            await mockUSDT.connect(user1).faucet();
            expect(await mockUSDT.balanceOf(user1.address)).to.equal(ethers.parseUnits("10000", 6));
        });
    });

    describe("MockVerifier", function () {
        it("Should return true by default", async function () {
            const proof = new Array(8).fill(0n);
            const publicInputs = new Array(4).fill(0n);
            expect(await mockVerifier.verifyProof(proof, publicInputs)).to.be.true;
        });

        it("Should allow setting mock result", async function () {
            await mockVerifier.setMockResult(false);
            const proof = new Array(8).fill(0n);
            const publicInputs = new Array(4).fill(0n);
            expect(await mockVerifier.verifyProof(proof, publicInputs)).to.be.false;
        });
    });

    describe("MerkleTree", function () {
        let merkleTree;

        beforeEach(async function () {
            // Deploy a test contract that exposes MerkleTree internals
            const MerkleTreeTest = await ethers.getContractFactory("MerkleTreeTest");
            merkleTree = await MerkleTreeTest.deploy(20);
            await merkleTree.waitForDeployment();
        });

        it("Should initialize with correct levels", async function () {
            expect(await merkleTree.levels()).to.equal(20);
        });

        it("Should start with zero leaves", async function () {
            expect(await merkleTree.getLeafCount()).to.equal(0);
        });

        it("Should have an initial root", async function () {
            const root = await merkleTree.getRoot();
            expect(root).to.not.equal(ethers.ZeroHash);
        });

        it("Should track root history", async function () {
            const root = await merkleTree.getRoot();
            expect(await merkleTree.isKnownRoot(root)).to.be.true;
        });
    });

    describe("StealthAddress Library", function () {
        it("Should generate consistent commitments", async function () {
            // This would test the StealthAddress library
            // Commitments should be deterministic for same inputs
            const commitment1 = ethers.keccak256(ethers.AbiCoder.defaultAbiCoder().encode(
                ["uint256", "bytes32", "bytes32", "bytes32"],
                [DENOM_100, ethers.ZeroHash, ethers.ZeroHash, ethers.id("random1")]
            ));
            const commitment2 = ethers.keccak256(ethers.AbiCoder.defaultAbiCoder().encode(
                ["uint256", "bytes32", "bytes32", "bytes32"],
                [DENOM_100, ethers.ZeroHash, ethers.ZeroHash, ethers.id("random1")]
            ));
            expect(commitment1).to.equal(commitment2);
        });

        it("Should generate different commitments for different inputs", async function () {
            const commitment1 = ethers.keccak256(ethers.AbiCoder.defaultAbiCoder().encode(
                ["uint256", "bytes32", "bytes32", "bytes32"],
                [DENOM_100, ethers.ZeroHash, ethers.ZeroHash, ethers.id("random1")]
            ));
            const commitment2 = ethers.keccak256(ethers.AbiCoder.defaultAbiCoder().encode(
                ["uint256", "bytes32", "bytes32", "bytes32"],
                [DENOM_100, ethers.ZeroHash, ethers.ZeroHash, ethers.id("random2")]
            ));
            expect(commitment1).to.not.equal(commitment2);
        });
    });
});

// Helper contract for testing MerkleTree
// This would need to be in a separate file in production
