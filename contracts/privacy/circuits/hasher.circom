pragma circom 2.0.0;

// Hasher circuits for WATTx Privacy Pool
// Uses MiMC hash (ZK-friendly) - can be replaced with Poseidon

include "../node_modules/circomlib/circuits/mimcsponge.circom";

// Hash 2 inputs using MiMC
template Hasher2() {
    signal input in[2];
    signal output hash;

    component hasher = MiMCSponge(2, 220, 1);
    hasher.ins[0] <== in[0];
    hasher.ins[1] <== in[1];
    hasher.k <== 0;

    hash <== hasher.outs[0];
}

// Hash 3 inputs using MiMC
template Hasher3() {
    signal input in[3];
    signal output hash;

    component hasher = MiMCSponge(3, 220, 1);
    hasher.ins[0] <== in[0];
    hasher.ins[1] <== in[1];
    hasher.ins[2] <== in[2];
    hasher.k <== 0;

    hash <== hasher.outs[0];
}

// Hash commitment: H(nullifier, secret, amount)
template CommitmentHasher() {
    signal input nullifier;
    signal input secret;
    signal input amount;
    signal output commitment;
    signal output nullifierHash;

    // Commitment = H(nullifier, secret, amount)
    component commitmentHasher = Hasher3();
    commitmentHasher.in[0] <== nullifier;
    commitmentHasher.in[1] <== secret;
    commitmentHasher.in[2] <== amount;
    commitment <== commitmentHasher.hash;

    // NullifierHash = H(nullifier, leafIndex) - computed in withdraw circuit
    // For now, just hash the nullifier
    component nullifierHasher = Hasher2();
    nullifierHasher.in[0] <== nullifier;
    nullifierHasher.in[1] <== nullifier; // Will be replaced with leafIndex
    nullifierHash <== nullifierHasher.hash;
}
