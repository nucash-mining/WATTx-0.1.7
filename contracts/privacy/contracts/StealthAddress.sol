// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/**
 * @title StealthAddress
 * @notice Library for stealth address generation and verification
 * @dev Implements DKSAP (Dual-Key Stealth Address Protocol)
 *
 * Stealth addresses allow a sender to create a one-time address that only
 * the intended recipient can spend from, without revealing their identity.
 *
 * Protocol:
 * 1. Recipient publishes: (viewKey, spendKey) - their public keys
 * 2. Sender generates ephemeral keypair: (r, R = r*G)
 * 3. Sender computes shared secret: S = r * viewKey
 * 4. Sender derives stealth pubkey: P = spendKey + hash(S)*G
 * 5. Recipient can compute P using their private keys
 * 6. Only recipient knows the private key for P
 */
library StealthAddress {

    // secp256k1 curve order
    uint256 constant private N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141;

    // Generator point G (compressed form not used, we work with coordinates)
    uint256 constant private Gx = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798;
    uint256 constant private Gy = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8;

    /**
     * @notice Generate a stealth commitment for deposit
     * @param amount The deposit amount
     * @param stealthPubKeyX X coordinate of recipient's stealth public key
     * @param stealthPubKeyY Y coordinate of recipient's stealth public key
     * @param randomness Random value provided by depositor
     * @return commitment The commitment hash
     */
    function generateCommitment(
        uint256 amount,
        bytes32 stealthPubKeyX,
        bytes32 stealthPubKeyY,
        bytes32 randomness
    ) internal pure returns (bytes32 commitment) {
        // Pedersen-style commitment: H(amount || stealthPubKey || randomness)
        return keccak256(abi.encodePacked(
            amount,
            stealthPubKeyX,
            stealthPubKeyY,
            randomness
        ));
    }

    /**
     * @notice Generate nullifier from secret and commitment
     * @dev Nullifier = hash(secret || leafIndex)
     * @param secret The depositor's secret
     * @param leafIndex The Merkle tree leaf index
     * @return nullifier The nullifier hash
     */
    function generateNullifier(
        bytes32 secret,
        uint256 leafIndex
    ) internal pure returns (bytes32 nullifier) {
        return keccak256(abi.encodePacked(secret, leafIndex));
    }

    /**
     * @notice Verify that a stealth public key is on the curve
     * @param x X coordinate
     * @param y Y coordinate
     * @return valid True if point is on secp256k1 curve
     */
    function isValidPoint(uint256 x, uint256 y) internal pure returns (bool valid) {
        // secp256k1: y^2 = x^3 + 7 (mod p)
        uint256 p = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F;

        if (x >= p || y >= p) return false;
        if (x == 0 && y == 0) return false;

        // Check y^2 = x^3 + 7 (mod p)
        uint256 lhs = mulmod(y, y, p);
        uint256 rhs = addmod(mulmod(mulmod(x, x, p), x, p), 7, p);

        return lhs == rhs;
    }

    /**
     * @notice Hash to derive shared secret components
     * @param sharedSecretX X coordinate of ECDH shared secret
     * @param sharedSecretY Y coordinate of ECDH shared secret
     * @return derived Derived key material
     */
    function deriveFromSharedSecret(
        bytes32 sharedSecretX,
        bytes32 sharedSecretY
    ) internal pure returns (bytes32 derived) {
        return keccak256(abi.encodePacked(
            "WATTx_STEALTH_DERIVE_v1",
            sharedSecretX,
            sharedSecretY
        ));
    }

    /**
     * @notice Create view tag for efficient scanning
     * @dev Recipients can quickly filter transactions using view tags
     * @param sharedSecretX X coordinate of shared secret
     * @return viewTag First byte of hash (for filtering)
     */
    function createViewTag(bytes32 sharedSecretX) internal pure returns (uint8 viewTag) {
        return uint8(uint256(keccak256(abi.encodePacked("VIEW_TAG", sharedSecretX))));
    }
}
