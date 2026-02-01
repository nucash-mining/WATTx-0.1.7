// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/**
 * @title MerkleTree
 * @notice Incremental Merkle tree for commitment storage
 * @dev Based on Tornado Cash's Merkle tree implementation
 *
 * Uses Poseidon hash for ZK-friendliness (can be swapped for Keccak256)
 */
contract MerkleTree {
    // ============ Constants ============

    uint256 public immutable levels;

    // Zero values for each level (precomputed)
    bytes32[] public zeros;

    // ============ State ============

    // Current index of next leaf to be inserted
    uint256 public nextIndex;

    // Filled subtrees - stores one node per level
    bytes32[] public filledSubtrees;

    // Historical roots for verification
    bytes32[] public roots;
    mapping(bytes32 => bool) public rootHistory;
    uint256 public constant ROOT_HISTORY_SIZE = 100;

    // ============ Events ============

    event LeafInserted(bytes32 indexed leaf, uint256 indexed leafIndex, bytes32 indexed newRoot);

    // ============ Constructor ============

    constructor(uint256 _levels) {
        require(_levels > 0 && _levels <= 32, "Invalid levels");
        levels = _levels;

        // Initialize zero values for each level
        // zeros[0] = hash of empty leaf
        // zeros[i] = hash(zeros[i-1], zeros[i-1])
        bytes32 currentZero = bytes32(0);
        zeros.push(currentZero);
        filledSubtrees.push(currentZero);

        for (uint256 i = 1; i < _levels; i++) {
            currentZero = _hashPair(currentZero, currentZero);
            zeros.push(currentZero);
            filledSubtrees.push(currentZero);
        }

        // Initialize with empty root
        bytes32 emptyRoot = _hashPair(currentZero, currentZero);
        roots.push(emptyRoot);
        rootHistory[emptyRoot] = true;
    }

    // ============ Insert Function ============

    /**
     * @notice Insert a new leaf into the tree
     * @param leaf The commitment to insert
     * @return index The index of the inserted leaf
     */
    function _insert(bytes32 leaf) internal returns (uint256 index) {
        uint256 _nextIndex = nextIndex;
        require(_nextIndex < 2**levels, "Tree is full");

        index = _nextIndex;
        bytes32 currentHash = leaf;
        uint256 currentIndex = _nextIndex;

        // Update the path from leaf to root
        for (uint256 i = 0; i < levels; i++) {
            if (currentIndex % 2 == 0) {
                // Left child - store for later use
                filledSubtrees[i] = currentHash;
                currentHash = _hashPair(currentHash, zeros[i]);
            } else {
                // Right child - use stored left sibling
                currentHash = _hashPair(filledSubtrees[i], currentHash);
            }
            currentIndex = currentIndex / 2;
        }

        // Store new root
        _addRoot(currentHash);
        nextIndex = _nextIndex + 1;

        emit LeafInserted(leaf, index, currentHash);
        return index;
    }

    /**
     * @notice Add a new root to history
     */
    function _addRoot(bytes32 root) internal {
        roots.push(root);
        rootHistory[root] = true;

        // Remove old roots if history is too long
        if (roots.length > ROOT_HISTORY_SIZE) {
            bytes32 oldRoot = roots[roots.length - ROOT_HISTORY_SIZE - 1];
            rootHistory[oldRoot] = false;
        }
    }

    // ============ Hash Function ============

    /**
     * @notice Hash two nodes together
     * @dev Can be replaced with Poseidon for ZK-friendliness
     */
    function _hashPair(bytes32 left, bytes32 right) internal pure returns (bytes32) {
        return keccak256(abi.encodePacked(left, right));
    }

    // ============ View Functions ============

    /**
     * @notice Get the current Merkle root
     */
    function getRoot() public view returns (bytes32) {
        return roots[roots.length - 1];
    }

    /**
     * @notice Check if a root is in recent history
     */
    function isKnownRoot(bytes32 root) public view returns (bool) {
        return rootHistory[root];
    }

    /**
     * @notice Get the number of leaves in the tree
     */
    function getLeafCount() external view returns (uint256) {
        return nextIndex;
    }

    /**
     * @notice Get all historical roots
     */
    function getRoots() external view returns (bytes32[] memory) {
        return roots;
    }

    /**
     * @notice Verify a Merkle proof
     * @param leaf The leaf to verify
     * @param pathElements The sibling nodes along the path
     * @param pathIndices The position (0=left, 1=right) at each level
     * @param root The root to verify against
     */
    function verifyMerkleProof(
        bytes32 leaf,
        bytes32[] calldata pathElements,
        uint256[] calldata pathIndices,
        bytes32 root
    ) external pure returns (bool) {
        require(pathElements.length == pathIndices.length, "Invalid proof length");

        bytes32 currentHash = leaf;
        for (uint256 i = 0; i < pathElements.length; i++) {
            if (pathIndices[i] == 0) {
                currentHash = _hashPair(currentHash, pathElements[i]);
            } else {
                currentHash = _hashPair(pathElements[i], currentHash);
            }
        }

        return currentHash == root;
    }
}
