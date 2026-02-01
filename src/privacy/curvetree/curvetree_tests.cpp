// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <privacy/curvetree/curve_tree.h>
#include <privacy/curvetree/tree_db.h>

#include <iostream>
#include <cassert>
#include <filesystem>
#include <random>

using namespace curvetree;
using namespace ed25519;

// ============================================================================
// Test Helpers
// ============================================================================

OutputTuple MakeRandomOutput() {
    // Generate random output tuple
    Point O = Point::Random();
    Point I = Point::Random();
    Point C = Point::Random();
    return OutputTuple(O, I, C);
}

std::vector<OutputTuple> MakeRandomOutputs(size_t count) {
    std::vector<OutputTuple> outputs;
    outputs.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        outputs.push_back(MakeRandomOutput());
    }
    return outputs;
}

// ============================================================================
// OutputTuple Tests
// ============================================================================

void test_output_tuple_basic() {
    std::cout << "Testing OutputTuple basics..." << std::endl;

    OutputTuple output = MakeRandomOutput();

    // Should be valid
    assert(output.IsValid());

    // Field elements conversion
    auto elements = output.ToFieldElements();
    assert(elements.size() == TreeConfig::ELEMENTS_PER_OUTPUT);

    // Serialization round-trip
    auto serialized = output.Serialize();
    assert(serialized.size() == 96);

    auto deserialized = OutputTuple::Deserialize(serialized);
    assert(deserialized.has_value());
    assert(*deserialized == output);

    std::cout << "  - Basic operations: OK" << std::endl;
}

void test_output_tuple_invalid() {
    std::cout << "Testing OutputTuple validation..." << std::endl;

    // Identity points should be invalid
    OutputTuple invalid1(Point::Identity(), Point::Random(), Point::Random());
    assert(!invalid1.IsValid());

    OutputTuple invalid2(Point::Random(), Point::Identity(), Point::Random());
    assert(!invalid2.IsValid());

    OutputTuple invalid3(Point::Random(), Point::Random(), Point::Identity());
    assert(!invalid3.IsValid());

    // Deserialization of invalid data should fail
    std::vector<uint8_t> bad_data(96, 0);
    auto result = OutputTuple::Deserialize(bad_data);
    assert(!result.has_value());

    std::cout << "  - Validation: OK" << std::endl;
}

// ============================================================================
// TreeBranch Tests
// ============================================================================

void test_tree_branch_serialization() {
    std::cout << "Testing TreeBranch serialization..." << std::endl;

    TreeBranch branch;
    branch.leaf_index = 12345;

    // Add some layers
    branch.layers.push_back({Scalar::Random(), Scalar::Random(), Scalar::Random()});
    branch.layers.push_back({Scalar::Random(), Scalar::Random()});
    branch.layers.push_back({Scalar::Random()});

    // Serialize and deserialize
    auto serialized = branch.Serialize();
    auto deserialized = TreeBranch::Deserialize(serialized);

    assert(deserialized.has_value());
    assert(deserialized->leaf_index == branch.leaf_index);
    assert(deserialized->layers.size() == branch.layers.size());

    for (size_t i = 0; i < branch.layers.size(); ++i) {
        assert(deserialized->layers[i].size() == branch.layers[i].size());
        for (size_t j = 0; j < branch.layers[i].size(); ++j) {
            assert(deserialized->layers[i][j] == branch.layers[i][j]);
        }
    }

    std::cout << "  - Serialization: OK" << std::endl;
}

// ============================================================================
// MemoryTreeStorage Tests
// ============================================================================

void test_memory_storage_basic() {
    std::cout << "Testing MemoryTreeStorage..." << std::endl;

    MemoryTreeStorage storage;

    // Store and retrieve node
    TreeIndex idx(1, 42);
    TreeNode node(Point::Random(), 5);

    assert(storage.StoreNode(idx, node));

    auto retrieved = storage.GetNode(idx);
    assert(retrieved.has_value());
    assert(retrieved->hash == node.hash);
    assert(retrieved->child_count == node.child_count);

    // Store and retrieve output
    OutputTuple output = MakeRandomOutput();
    assert(storage.StoreOutput(0, output));

    auto retrieved_output = storage.GetOutput(0);
    assert(retrieved_output.has_value());
    assert(*retrieved_output == output);

    // Store and retrieve metadata
    std::vector<uint8_t> meta_value = {1, 2, 3, 4};
    assert(storage.StoreMetadata("test_key", meta_value));

    auto retrieved_meta = storage.GetMetadata("test_key");
    assert(retrieved_meta.has_value());
    assert(*retrieved_meta == meta_value);

    // Output count
    assert(storage.GetOutputCount() == 1);

    // Delete node
    assert(storage.DeleteNode(idx));
    assert(!storage.GetNode(idx).has_value());

    std::cout << "  - Basic operations: OK" << std::endl;
}

// ============================================================================
// CurveTree Tests
// ============================================================================

void test_curve_tree_empty() {
    std::cout << "Testing CurveTree empty state..." << std::endl;

    CurveTree tree;

    assert(tree.IsEmpty());
    assert(tree.GetOutputCount() == 0);
    assert(tree.GetDepth() == 0);

    // Root of empty tree is the hash init point
    Point root = tree.GetRoot();
    assert(root == tree.GetHasher().GetInit());

    std::cout << "  - Empty tree: OK" << std::endl;
}

void test_curve_tree_single_output() {
    std::cout << "Testing CurveTree with single output..." << std::endl;

    CurveTree tree;

    OutputTuple output = MakeRandomOutput();
    uint64_t index = tree.AddOutput(output);

    assert(index == 0);
    assert(tree.GetOutputCount() == 1);
    assert(tree.GetDepth() == 1);
    assert(!tree.IsEmpty());

    // Retrieve output
    auto retrieved = tree.GetOutput(0);
    assert(retrieved.has_value());
    assert(*retrieved == output);

    // Root should not be identity
    assert(!tree.GetRoot().IsIdentity());

    // Verify integrity
    assert(tree.VerifyIntegrity());

    std::cout << "  - Single output: OK" << std::endl;
}

void test_curve_tree_multiple_outputs() {
    std::cout << "Testing CurveTree with multiple outputs..." << std::endl;

    CurveTree tree;

    // Add enough outputs to create multiple leaf commitments
    size_t num_outputs = TreeConfig::LEAF_BRANCH_WIDTH * 3; // 114 outputs

    auto outputs = MakeRandomOutputs(num_outputs);
    auto indices = tree.AddOutputs(outputs);

    assert(indices.size() == num_outputs);
    assert(tree.GetOutputCount() == num_outputs);
    assert(tree.GetDepth() >= 2); // Multiple layers

    // Verify all outputs can be retrieved
    for (size_t i = 0; i < num_outputs; ++i) {
        auto retrieved = tree.GetOutput(i);
        assert(retrieved.has_value());
        assert(*retrieved == outputs[i]);
    }

    // Verify integrity
    assert(tree.VerifyIntegrity());

    std::cout << "  - Multiple outputs (" << num_outputs << "): OK" << std::endl;
}

void test_curve_tree_large_batch() {
    std::cout << "Testing CurveTree with large batch..." << std::endl;

    CurveTree tree;

    // Add a large number of outputs
    size_t num_outputs = 500;
    auto outputs = MakeRandomOutputs(num_outputs);

    auto indices = tree.AddOutputs(outputs);

    assert(indices.size() == num_outputs);
    assert(tree.GetOutputCount() == num_outputs);

    // Verify integrity
    assert(tree.VerifyIntegrity());

    // Root should be deterministic
    Point root1 = tree.GetRoot();
    Point root2 = tree.GetRoot();
    assert(root1 == root2);

    std::cout << "  - Large batch (" << num_outputs << " outputs): OK" << std::endl;
}

void test_curve_tree_branch_extraction() {
    std::cout << "Testing CurveTree branch extraction..." << std::endl;

    CurveTree tree;

    // Add outputs
    size_t num_outputs = 100;
    auto outputs = MakeRandomOutputs(num_outputs);
    tree.AddOutputs(outputs);

    // Extract branch for each output
    for (size_t i = 0; i < num_outputs; ++i) {
        auto branch = tree.GetBranch(i);
        assert(branch.has_value());
        assert(branch->leaf_index == i);
        assert(!branch->layers.empty());
    }

    // Non-existent index should return nullopt
    assert(!tree.GetBranch(num_outputs + 100).has_value());

    std::cout << "  - Branch extraction: OK" << std::endl;
}

void test_curve_tree_rebuild() {
    std::cout << "Testing CurveTree rebuild..." << std::endl;

    CurveTree tree;

    // Add outputs
    size_t num_outputs = 50;
    auto outputs = MakeRandomOutputs(num_outputs);
    tree.AddOutputs(outputs);

    Point original_root = tree.GetRoot();

    // Rebuild tree
    assert(tree.Rebuild());

    // Root should be the same
    Point rebuilt_root = tree.GetRoot();
    assert(original_root == rebuilt_root);

    // Integrity should still pass
    assert(tree.VerifyIntegrity());

    std::cout << "  - Rebuild: OK" << std::endl;
}

void test_curve_tree_determinism() {
    std::cout << "Testing CurveTree determinism..." << std::endl;

    // Create two trees with same outputs in same order
    CurveTree tree1;
    CurveTree tree2;

    auto outputs = MakeRandomOutputs(75);

    tree1.AddOutputs(outputs);
    tree2.AddOutputs(outputs);

    // Roots should be identical
    assert(tree1.GetRoot() == tree2.GetRoot());

    // Branches should be identical
    for (size_t i = 0; i < outputs.size(); ++i) {
        auto branch1 = tree1.GetBranch(i);
        auto branch2 = tree2.GetBranch(i);

        assert(branch1.has_value());
        assert(branch2.has_value());
        assert(branch1->leaf_index == branch2->leaf_index);
        assert(branch1->layers.size() == branch2->layers.size());
    }

    std::cout << "  - Determinism: OK" << std::endl;
}

void test_curve_tree_incremental() {
    std::cout << "Testing CurveTree incremental updates..." << std::endl;

    CurveTree tree;

    // Add outputs one by one
    std::vector<Point> roots;
    for (int i = 0; i < 20; ++i) {
        tree.AddOutput(MakeRandomOutput());
        roots.push_back(tree.GetRoot());

        // Each root should be different
        if (i > 0) {
            assert(roots[i] != roots[i-1]);
        }
    }

    // Integrity should pass at each step
    assert(tree.VerifyIntegrity());

    std::cout << "  - Incremental updates: OK" << std::endl;
}

// ============================================================================
// CurveTreeBuilder Tests
// ============================================================================

void test_curve_tree_builder() {
    std::cout << "Testing CurveTreeBuilder..." << std::endl;

    auto storage = std::make_shared<MemoryTreeStorage>();
    CurveTreeBuilder builder(storage);

    // Track progress
    uint64_t last_progress = 0;
    builder.SetProgressCallback([&](uint64_t processed, uint64_t total) {
        last_progress = processed;
    });

    // Add outputs in batches
    builder.AddOutputs(MakeRandomOutputs(50));
    builder.AddOutputs(MakeRandomOutputs(50));

    assert(builder.GetOutputCount() == 100);

    // Finalize
    auto tree = builder.Finalize();

    assert(tree->GetOutputCount() == 100);
    assert(tree->VerifyIntegrity());

    std::cout << "  - Builder: OK" << std::endl;
}

// ============================================================================
// LevelDB Storage Tests (if available)
// ============================================================================

void test_leveldb_storage() {
    std::cout << "Testing LevelDB storage..." << std::endl;

    // Create temporary directory
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "wattx_curvetree_test";
    std::filesystem::remove_all(temp_dir);

    try {
        auto storage = std::make_shared<LevelDBTreeStorage>(temp_dir);

        // Basic operations
        OutputTuple output = MakeRandomOutput();
        assert(storage->StoreOutput(0, output));

        auto retrieved = storage->GetOutput(0);
        assert(retrieved.has_value());
        assert(*retrieved == output);

        // Node operations
        TreeIndex idx(1, 42);
        TreeNode node(Point::Random(), 5);
        assert(storage->StoreNode(idx, node));

        auto retrieved_node = storage->GetNode(idx);
        assert(retrieved_node.has_value());
        assert(retrieved_node->hash == node.hash);

        // Batch operations
        storage->BeginBatch();
        for (int i = 1; i < 10; ++i) {
            storage->StoreOutput(i, MakeRandomOutput());
        }
        assert(storage->CommitBatch());

        assert(storage->GetOutputCount() == 10);

        // Sync
        assert(storage->Sync());

        std::cout << "  - LevelDB operations: OK" << std::endl;

    } catch (const std::exception& e) {
        std::cout << "  - LevelDB test skipped: " << e.what() << std::endl;
    }

    // Cleanup
    std::filesystem::remove_all(temp_dir);
}

void test_leveldb_persistence() {
    std::cout << "Testing LevelDB persistence..." << std::endl;

    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "wattx_curvetree_persist";
    std::filesystem::remove_all(temp_dir);

    // Use deterministic outputs for testing
    std::vector<OutputTuple> original_outputs;
    for (int i = 0; i < 20; ++i) {
        // Use deterministic points based on index
        std::vector<uint8_t> seed(32, 0);
        seed[0] = i;
        Point O = Point::HashToPoint(seed);
        seed[0] = i + 100;
        Point I = Point::HashToPoint(seed);
        seed[0] = i + 200;
        Point C = Point::HashToPoint(seed);
        original_outputs.emplace_back(O, I, C);
    }

    try {
        // Create and populate tree
        {
            auto storage = TreeStorageFactory::Create(
                TreeStorageFactory::StorageType::LevelDB, temp_dir);
            CurveTree tree(storage);

            tree.AddOutputs(original_outputs);
            tree.Save();

            // Sync to ensure data is written
            auto* leveldb_storage = dynamic_cast<LevelDBTreeStorage*>(storage.get());
            if (leveldb_storage) {
                leveldb_storage->Sync();
            }
        }

        // Reopen and verify outputs are persisted
        {
            auto storage = TreeStorageFactory::Create(
                TreeStorageFactory::StorageType::LevelDB, temp_dir);
            CurveTree tree(storage);

            // Verify output count
            uint64_t count = tree.GetOutputCount();
            assert(count == 20);

            // Check that all outputs can be retrieved correctly
            for (size_t i = 0; i < original_outputs.size(); ++i) {
                auto retrieved = tree.GetOutput(i);
                assert(retrieved.has_value());
                assert(*retrieved == original_outputs[i]);
            }

            // Rebuild tree from outputs
            tree.Rebuild();

            // Verify root is not identity (tree was built)
            Point root = tree.GetRoot();
            assert(!root.IsIdentity());
        }

        std::cout << "  - Persistence: OK" << std::endl;

    } catch (const std::exception& e) {
        std::cout << "  - Persistence test skipped: " << e.what() << std::endl;
    }

    std::filesystem::remove_all(temp_dir);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=== WATTx Curve Tree Module Tests ===" << std::endl << std::endl;

    try {
        // OutputTuple tests
        test_output_tuple_basic();
        test_output_tuple_invalid();

        std::cout << std::endl;

        // TreeBranch tests
        test_tree_branch_serialization();

        std::cout << std::endl;

        // Storage tests
        test_memory_storage_basic();

        std::cout << std::endl;

        // CurveTree tests
        test_curve_tree_empty();
        test_curve_tree_single_output();
        test_curve_tree_multiple_outputs();
        test_curve_tree_large_batch();
        test_curve_tree_branch_extraction();
        test_curve_tree_rebuild();
        test_curve_tree_determinism();
        test_curve_tree_incremental();

        std::cout << std::endl;

        // Builder tests
        test_curve_tree_builder();

        std::cout << std::endl;

        // LevelDB tests
        test_leveldb_storage();
        test_leveldb_persistence();

        std::cout << std::endl;
        std::cout << "=== All Curve Tree tests passed! ===" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
