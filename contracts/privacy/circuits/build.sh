#!/bin/bash
# Build script for WATTx Privacy Pool ZK circuits

set -e

CIRCUIT_NAME="withdraw"
BUILD_DIR="./build"
PTAU_FILE="powersOfTau28_hez_final_16.ptau"

echo "=== WATTx Privacy Pool Circuit Builder ==="
echo ""

# Create build directory
mkdir -p $BUILD_DIR

# Step 1: Download Powers of Tau (if not exists)
if [ ! -f "$BUILD_DIR/$PTAU_FILE" ]; then
    echo "1. Downloading Powers of Tau ceremony file..."
    curl -L -o "$BUILD_DIR/$PTAU_FILE" \
        "https://hermez.s3-eu-west-1.amazonaws.com/$PTAU_FILE"
else
    echo "1. Powers of Tau file already exists"
fi

# Step 2: Compile circuit
echo ""
echo "2. Compiling circuit..."
circom $CIRCUIT_NAME.circom --r1cs --wasm --sym -o $BUILD_DIR

# Step 3: View circuit info
echo ""
echo "3. Circuit info:"
snarkjs r1cs info $BUILD_DIR/$CIRCUIT_NAME.r1cs

# Step 4: Generate proving key (zkey)
echo ""
echo "4. Generating proving key..."
snarkjs groth16 setup $BUILD_DIR/$CIRCUIT_NAME.r1cs $BUILD_DIR/$PTAU_FILE $BUILD_DIR/${CIRCUIT_NAME}_0000.zkey

# Step 5: Contribute to ceremony (adds randomness)
echo ""
echo "5. Contributing randomness to ceremony..."
snarkjs zkey contribute $BUILD_DIR/${CIRCUIT_NAME}_0000.zkey $BUILD_DIR/${CIRCUIT_NAME}_final.zkey \
    --name="WATTx Contribution" -v -e="$(head -c 64 /dev/urandom | xxd -p)"

# Step 6: Export verification key
echo ""
echo "6. Exporting verification key..."
snarkjs zkey export verificationkey $BUILD_DIR/${CIRCUIT_NAME}_final.zkey $BUILD_DIR/verification_key.json

# Step 7: Generate Solidity verifier
echo ""
echo "7. Generating Solidity verifier..."
snarkjs zkey export solidityverifier $BUILD_DIR/${CIRCUIT_NAME}_final.zkey ../contracts/Groth16Verifier.sol

echo ""
echo "=== BUILD COMPLETE ==="
echo ""
echo "Generated files:"
echo "  - $BUILD_DIR/$CIRCUIT_NAME.r1cs (circuit)"
echo "  - $BUILD_DIR/${CIRCUIT_NAME}_js/ (WASM prover)"
echo "  - $BUILD_DIR/${CIRCUIT_NAME}_final.zkey (proving key)"
echo "  - $BUILD_DIR/verification_key.json"
echo "  - ../contracts/Groth16Verifier.sol"
echo ""
echo "To generate a proof:"
echo "  snarkjs groth16 fullprove input.json $BUILD_DIR/${CIRCUIT_NAME}_js/${CIRCUIT_NAME}.wasm $BUILD_DIR/${CIRCUIT_NAME}_final.zkey proof.json public.json"
echo ""
echo "To verify a proof:"
echo "  snarkjs groth16 verify $BUILD_DIR/verification_key.json public.json proof.json"
