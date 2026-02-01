// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

import "../MerkleTree.sol";

/**
 * @title MerkleTreeTest
 * @notice Test contract that exposes MerkleTree internal functions
 */
contract MerkleTreeTest is MerkleTree {

    constructor(uint256 _levels) MerkleTree(_levels) {}

    /**
     * @notice Public wrapper for _insert
     */
    function insert(bytes32 leaf) external returns (uint256) {
        return _insert(leaf);
    }

    /**
     * @notice Insert multiple leaves
     */
    function insertBatch(bytes32[] calldata leaves) external returns (uint256[] memory indices) {
        indices = new uint256[](leaves.length);
        for (uint256 i = 0; i < leaves.length; i++) {
            indices[i] = _insert(leaves[i]);
        }
    }
}
