pragma circom 2.0.0;

// Merkle Tree verification circuit for WATTx Privacy Pool
// Proves membership in a Merkle tree without revealing which leaf

include "../node_modules/circomlib/circuits/mimcsponge.circom";
include "../node_modules/circomlib/circuits/bitify.circom";

// Hash two nodes in Merkle tree
template HashLeftRight() {
    signal input left;
    signal input right;
    signal output hash;

    component hasher = MiMCSponge(2, 220, 1);
    hasher.ins[0] <== left;
    hasher.ins[1] <== right;
    hasher.k <== 0;

    hash <== hasher.outs[0];
}

// Select between two inputs based on selector bit
// If s == 0: out[0] = in[0], out[1] = in[1]
// If s == 1: out[0] = in[1], out[1] = in[0]
template DualMux() {
    signal input in[2];
    signal input s;
    signal output out[2];

    s * (1 - s) === 0;  // s must be 0 or 1

    out[0] <== (in[1] - in[0]) * s + in[0];
    out[1] <== (in[0] - in[1]) * s + in[1];
}

// Verify Merkle proof
// levels: depth of the tree
template MerkleTreeChecker(levels) {
    signal input leaf;
    signal input root;
    signal input pathElements[levels];
    signal input pathIndices[levels];

    component selectors[levels];
    component hashers[levels];

    signal levelHashes[levels + 1];
    levelHashes[0] <== leaf;

    for (var i = 0; i < levels; i++) {
        selectors[i] = DualMux();
        selectors[i].in[0] <== levelHashes[i];
        selectors[i].in[1] <== pathElements[i];
        selectors[i].s <== pathIndices[i];

        hashers[i] = HashLeftRight();
        hashers[i].left <== selectors[i].out[0];
        hashers[i].right <== selectors[i].out[1];

        levelHashes[i + 1] <== hashers[i].hash;
    }

    // Verify computed root matches expected root
    root === levelHashes[levels];
}

// Compute leaf index from path indices
template LeafIndexFromPath(levels) {
    signal input pathIndices[levels];
    signal output index;

    signal runningIndex[levels + 1];
    runningIndex[0] <== 0;

    for (var i = 0; i < levels; i++) {
        // pathIndices[i] must be 0 or 1
        pathIndices[i] * (1 - pathIndices[i]) === 0;

        // Build index: pathIndices[0] is LSB
        runningIndex[i + 1] <== runningIndex[i] + pathIndices[i] * (1 << i);
    }

    index <== runningIndex[levels];
}
