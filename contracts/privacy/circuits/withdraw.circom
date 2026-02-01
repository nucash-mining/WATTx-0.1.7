pragma circom 2.0.0;

// Withdrawal circuit for WATTx Privacy Pool
// Proves:
// 1. Prover knows a commitment in the Merkle tree
// 2. Prover knows the preimage (nullifier, secret, amount) of the commitment
// 3. The nullifierHash is correctly computed
// 4. The amount matches the withdrawal amount

include "./merkleTree.circom";
include "../node_modules/circomlib/circuits/mimcsponge.circom";
include "../node_modules/circomlib/circuits/bitify.circom";

template Withdraw(levels) {
    // Public inputs (specified in main component)
    signal input root;           // Merkle root
    signal input nullifierHash;  // Hash of nullifier (to prevent double-spend)
    signal input amount;         // Withdrawal amount
    signal input recipient;      // Recipient address (for binding)

    // Private inputs (default in circom 2)
    signal input nullifier;          // Secret nullifier
    signal input secret;             // Secret value
    signal input pathElements[levels]; // Merkle proof siblings
    signal input pathIndices[levels];  // Merkle proof path (0=left, 1=right)

    // Step 1: Compute commitment = H(nullifier, secret, amount)
    component commitmentHasher = MiMCSponge(3, 220, 1);
    commitmentHasher.ins[0] <== nullifier;
    commitmentHasher.ins[1] <== secret;
    commitmentHasher.ins[2] <== amount;
    commitmentHasher.k <== 0;
    signal commitment;
    commitment <== commitmentHasher.outs[0];

    // Step 2: Compute leaf index from path
    component leafIndexer = LeafIndexFromPath(levels);
    for (var i = 0; i < levels; i++) {
        leafIndexer.pathIndices[i] <== pathIndices[i];
    }
    signal leafIndex;
    leafIndex <== leafIndexer.index;

    // Step 3: Compute nullifierHash = H(nullifier, leafIndex)
    component nullifierHashComputer = MiMCSponge(2, 220, 1);
    nullifierHashComputer.ins[0] <== nullifier;
    nullifierHashComputer.ins[1] <== leafIndex;
    nullifierHashComputer.k <== 0;

    // Verify nullifierHash matches public input
    nullifierHash === nullifierHashComputer.outs[0];

    // Step 4: Verify Merkle proof
    component merkleChecker = MerkleTreeChecker(levels);
    merkleChecker.leaf <== commitment;
    merkleChecker.root <== root;
    for (var i = 0; i < levels; i++) {
        merkleChecker.pathElements[i] <== pathElements[i];
        merkleChecker.pathIndices[i] <== pathIndices[i];
    }

    // Step 5: Bind recipient to proof (prevents front-running)
    // We just need to use the recipient signal so it's part of the proof
    signal recipientSquare;
    recipientSquare <== recipient * recipient;
}

// Main component with 20 levels (supports ~1M commitments)
// Public inputs: root, nullifierHash, amount, recipient
component main {public [root, nullifierHash, amount, recipient]} = Withdraw(20);
