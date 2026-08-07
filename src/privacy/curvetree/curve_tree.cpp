// Copyright (c) 2024-2026 The WATTx Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <privacy/curvetree/curve_tree.h>
#include <privacy/curvetree/curve_tree_hash.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

namespace curvetree {

// ============================================================================
// OutputTuple Implementation
// ============================================================================

bool OutputTuple::IsValid() const {
    return O.IsValid() && !O.IsIdentity() &&
           I.IsValid() && !I.IsIdentity() &&
           C.IsValid() && !C.IsIdentity();
}

std::vector<Scalar> OutputTuple::ToFieldElements() const {
    // For now, we use the compressed point bytes as field elements
    // In production, we'd extract actual x,y coordinates
    // This is a simplification - FCMP uses actual coordinate extraction

    std::vector<Scalar> elements;
    elements.reserve(TreeConfig::ELEMENTS_PER_OUTPUT);

    // O.x, O.y (using point bytes split into two scalars)
    elements.push_back(Scalar::FromBytesModOrder(O.data.data(), 16));
    elements.push_back(Scalar::FromBytesModOrder(O.data.data() + 16, 16));

    // I.x, I.y
    elements.push_back(Scalar::FromBytesModOrder(I.data.data(), 16));
    elements.push_back(Scalar::FromBytesModOrder(I.data.data() + 16, 16));

    // C.x, C.y
    elements.push_back(Scalar::FromBytesModOrder(C.data.data(), 16));
    elements.push_back(Scalar::FromBytesModOrder(C.data.data() + 16, 16));

    return elements;
}

std::vector<uint8_t> OutputTuple::Serialize() const {
    std::vector<uint8_t> data;
    data.reserve(96); // 3 * 32 bytes

    data.insert(data.end(), O.data.begin(), O.data.end());
    data.insert(data.end(), I.data.begin(), I.data.end());
    data.insert(data.end(), C.data.begin(), C.data.end());

    return data;
}

std::optional<OutputTuple> OutputTuple::Deserialize(const std::vector<uint8_t>& data) {
    if (data.size() != 96) {
        return std::nullopt;
    }

    OutputTuple output;
    std::memcpy(output.O.data.data(), data.data(), 32);
    std::memcpy(output.I.data.data(), data.data() + 32, 32);
    std::memcpy(output.C.data.data(), data.data() + 64, 32);

    if (!output.IsValid()) {
        return std::nullopt;
    }

    return output;
}

bool OutputTuple::operator==(const OutputTuple& other) const {
    return O == other.O && I == other.I && C == other.C;
}

// ============================================================================
// TreeBranch Implementation
// ============================================================================

std::vector<uint8_t> TreeBranch::Serialize() const {
    std::vector<uint8_t> data;

    // Leaf index (8 bytes)
    for (int i = 0; i < 8; ++i) {
        data.push_back((leaf_index >> (i * 8)) & 0xFF);
    }

    // Number of layers (4 bytes)
    uint32_t num_layers = static_cast<uint32_t>(layers.size());
    for (int i = 0; i < 4; ++i) {
        data.push_back((num_layers >> (i * 8)) & 0xFF);
    }

    // Each layer
    for (const auto& layer : layers) {
        // Number of elements in layer (4 bytes)
        uint32_t num_elements = static_cast<uint32_t>(layer.size());
        for (int i = 0; i < 4; ++i) {
            data.push_back((num_elements >> (i * 8)) & 0xFF);
        }

        // Elements: 1 curve tag + 32 bytes each. The tag is not optional --
        // the same bytes are a valid point on both curves and hash differently.
        for (const auto& h : layer) {
            data.push_back(static_cast<uint8_t>(h.curve));
            data.insert(data.end(), h.bytes.begin(), h.bytes.end());
        }
    }

    return data;
}

std::optional<TreeBranch> TreeBranch::Deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 12) {
        return std::nullopt;
    }

    TreeBranch branch;
    size_t offset = 0;

    // Leaf index
    branch.leaf_index = 0;
    for (int i = 0; i < 8; ++i) {
        branch.leaf_index |= static_cast<uint64_t>(data[offset++]) << (i * 8);
    }

    // Number of layers
    uint32_t num_layers = 0;
    for (int i = 0; i < 4; ++i) {
        num_layers |= static_cast<uint32_t>(data[offset++]) << (i * 8);
    }

    if (num_layers > TreeConfig::MAX_DEPTH) {
        return std::nullopt;
    }

    // Each layer
    branch.layers.reserve(num_layers);
    for (uint32_t l = 0; l < num_layers; ++l) {
        if (offset + 4 > data.size()) {
            return std::nullopt;
        }

        uint32_t num_elements = 0;
        for (int i = 0; i < 4; ++i) {
            num_elements |= static_cast<uint32_t>(data[offset++]) << (i * 8);
        }

        if (offset + num_elements * 32 > data.size()) {
            return std::nullopt;
        }

        std::vector<TreeHash> layer;
        layer.reserve(num_elements);
        for (uint32_t e = 0; e < num_elements; ++e) {
            TreeHash h;
            const uint8_t tag = data[offset];
            if (tag != static_cast<uint8_t>(TreeCurve::SELENE) &&
                tag != static_cast<uint8_t>(TreeCurve::HELIOS)) {
                return std::nullopt;
            }
            h.curve = static_cast<TreeCurve>(tag);
            std::memcpy(h.bytes.data(), data.data() + offset + 1, 32);
            offset += 33;
            layer.push_back(h);
        }
        branch.layers.push_back(std::move(layer));
    }

    return branch;
}

// ============================================================================
// MemoryTreeStorage Implementation
// ============================================================================

bool MemoryTreeStorage::StoreNode(const TreeIndex& index, const TreeNode& node) {
    m_nodes[index] = node;
    return true;
}

std::optional<TreeNode> MemoryTreeStorage::GetNode(const TreeIndex& index) {
    auto it = m_nodes.find(index);
    if (it != m_nodes.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool MemoryTreeStorage::DeleteNode(const TreeIndex& index) {
    return m_nodes.erase(index) > 0;
}

bool MemoryTreeStorage::StoreOutput(uint64_t index, const OutputTuple& output) {
    m_outputs[index] = output;
    return true;
}

std::optional<OutputTuple> MemoryTreeStorage::GetOutput(uint64_t index) {
    auto it = m_outputs.find(index);
    if (it != m_outputs.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool MemoryTreeStorage::DeleteOutput(uint64_t index) {
    return m_outputs.erase(index) > 0;
}

bool MemoryTreeStorage::StoreMetadata(const std::string& key, const std::vector<uint8_t>& value) {
    m_metadata[key] = value;
    return true;
}

std::optional<std::vector<uint8_t>> MemoryTreeStorage::GetMetadata(const std::string& key) {
    auto it = m_metadata.find(key);
    if (it != m_metadata.end()) {
        return it->second;
    }
    return std::nullopt;
}

// ============================================================================
// CurveTree Implementation
// ============================================================================

CurveTree::CurveTree(std::shared_ptr<ITreeStorage> storage)
    : m_storage(std::move(storage))
    , m_hasher("WATTx_CurveTree_v1")
    , m_cached_root(TreeHash{})
    , m_root_dirty(true)
    , m_output_count(0)
    , m_depth(0)
{
    // Try to load existing tree state
    Load();
}

CurveTree::CurveTree()
    : CurveTree(std::make_shared<MemoryTreeStorage>())
{
}

TreeHash CurveTree::GetRoot() const {
    if (m_output_count == 0) {
        // An empty tree has no root. A placeholder here would look like a real
        // root and match nothing.
        return TreeHash{};
    }

    if (m_root_dirty) {
        // Root is at the top layer (m_depth - 1, since layers are 0-indexed)
        uint32_t root_layer = m_depth > 0 ? m_depth - 1 : 0;
        auto node = m_storage->GetNode(TreeIndex(root_layer, 0));
        if (node) {
            m_cached_root = node->hash;
        } else {
            m_cached_root = TreeHash{};
        }
        m_root_dirty = false;
    }

    return m_cached_root;
}

uint64_t CurveTree::GetOutputCount() const {
    return m_output_count;
}

uint32_t CurveTree::GetDepth() const {
    return m_depth;
}

uint32_t CurveTree::CalculateDepth(uint64_t output_count) {
    if (output_count == 0) {
        return 0;
    }

    // Calculate how many leaf commitments we need
    uint64_t leaf_commits = (output_count + TreeConfig::LEAF_BRANCH_WIDTH - 1) /
                            TreeConfig::LEAF_BRANCH_WIDTH;

    // Calculate depth from leaf commits
    uint32_t depth = 1; // At least leaf layer
    uint64_t nodes = leaf_commits;

    while (nodes > 1) {
        nodes = (nodes + TreeConfig::INTERNAL_BRANCH_WIDTH - 1) /
                TreeConfig::INTERNAL_BRANCH_WIDTH;
        depth++;
    }

    return depth;
}

uint64_t CurveTree::AddOutput(const OutputTuple& output) {
    if (!output.IsValid()) {
        throw std::runtime_error("Invalid output tuple");
    }

    uint64_t index = m_output_count;

    // Store the output
    m_storage->StoreOutput(index, output);
    m_output_count++;

    // Update tree depth if needed
    uint32_t new_depth = CalculateDepth(m_output_count);
    if (new_depth > m_depth) {
        m_depth = new_depth;
    }

    // Update the path from this leaf to root
    UpdatePath(index);

    m_root_dirty = true;
    return index;
}

std::vector<uint64_t> CurveTree::AddOutputs(const std::vector<OutputTuple>& outputs) {
    std::vector<uint64_t> indices;
    indices.reserve(outputs.size());

    // Store the outputs and COMMIT before hashing anything.
    //
    // Hashing inside the same batch silently produced an empty tree: the storage
    // backend routes writes into the pending batch, but GetOutput reads straight
    // from the database, so ComputeLeafNode saw NONE of the outputs just written
    // and hashed an empty branch. The tree reported a growing output count with a
    // zero root -- and any membership proof would have been verified against
    // that zero root.
    //
    // The old code hid this: an empty branch returned the hasher's init point,
    // a plausible-looking non-zero value.
    m_storage->BeginBatch();
    for (const auto& output : outputs) {
        if (!output.IsValid()) {
            m_storage->AbortBatch();
            throw std::runtime_error("Invalid output tuple in batch");
        }

        uint64_t index = m_output_count;
        m_storage->StoreOutput(index, output);
        indices.push_back(index);
        m_output_count++;
    }
    if (!m_storage->CommitBatch()) {
        throw std::runtime_error("Failed to commit outputs to tree storage");
    }

    // Update depth
    m_depth = CalculateDepth(m_output_count);

    // Now the outputs are readable, so the hashes can be computed over them.
    m_storage->BeginBatch();
    if (outputs.size() > 100) {
        // Full rebuild is more efficient for large batches
        Rebuild();
    } else {
        // Update paths for each new output
        for (uint64_t idx : indices) {
            UpdatePath(idx);
        }
    }
    m_storage->CommitBatch();
    m_root_dirty = true;

    return indices;
}

std::optional<OutputTuple> CurveTree::GetOutput(uint64_t index) const {
    return m_storage->GetOutput(index);
}

bool CurveTree::HasOutput(uint64_t index) const {
    return m_storage->GetOutput(index).has_value();
}

std::optional<uint64_t> CurveTree::FindOutputIndex(const OutputTuple& output) const {
    // Searched newest-first: a wallet looks up a note it is about to spend, and
    // notes are far more often recent than ancient.
    for (uint64_t i = m_output_count; i-- > 0; ) {
        auto stored = m_storage->GetOutput(i);
        if (stored && *stored == output) {
            return i;
        }
    }
    return std::nullopt;
}

bool CurveTree::RemoveLastN(uint64_t count) {
    if (count == 0) return true;
    if (count > m_output_count) return false;

    m_storage->BeginBatch();

    // Delete outputs from the end
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t idx = m_output_count - 1 - i;
        m_storage->DeleteOutput(idx);
    }

    // Update output count
    m_output_count -= count;

    // Recalculate depth
    m_depth = CalculateDepth(m_output_count);

    // Rebuild affected tree portions
    // For simplicity and correctness, rebuild from the first affected leaf commitment
    if (m_output_count > 0) {
        uint64_t first_affected_leaf = m_output_count / TreeConfig::LEAF_BRANCH_WIDTH;
        // Rebuild from the affected leaf up
        uint64_t num_leaf_commits = (m_output_count + TreeConfig::LEAF_BRANCH_WIDTH - 1) /
                                     TreeConfig::LEAF_BRANCH_WIDTH;

        // Recompute affected leaf nodes
        for (uint64_t i = first_affected_leaf; i < num_leaf_commits; ++i) {
            TreeHash hash = ComputeLeafNode(i);
            uint64_t start = i * TreeConfig::LEAF_BRANCH_WIDTH;
            uint64_t end = std::min(start + TreeConfig::LEAF_BRANCH_WIDTH, m_output_count);
            m_storage->StoreNode(TreeIndex(0, i), TreeNode(hash, end - start));
        }

        // Clean up any orphaned leaf nodes beyond current range
        for (uint64_t i = num_leaf_commits; ; ++i) {
            if (!m_storage->DeleteNode(TreeIndex(0, i))) break;
        }

        // Rebuild internal layers
        uint64_t nodes_at_prev_layer = num_leaf_commits;
        for (uint32_t layer = 1; layer < m_depth; ++layer) {
            uint64_t nodes_at_layer = (nodes_at_prev_layer + TreeConfig::INTERNAL_BRANCH_WIDTH - 1) /
                                       TreeConfig::INTERNAL_BRANCH_WIDTH;

            for (uint64_t i = 0; i < nodes_at_layer; ++i) {
                auto children = GetChildren(TreeIndex(layer, i));
                if (!children.empty()) {
                    TreeHash hash = ComputeNodeHash(children);
                    m_storage->StoreNode(TreeIndex(layer, i), TreeNode(hash, children.size()));
                }
            }

            // Clean up orphaned nodes
            for (uint64_t i = nodes_at_layer; ; ++i) {
                if (!m_storage->DeleteNode(TreeIndex(layer, i))) break;
            }

            nodes_at_prev_layer = nodes_at_layer;
        }
    }

    m_storage->CommitBatch();
    m_root_dirty = true;
    Save();

    return true;
}

TreeHash CurveTree::ComputeNodeHash(const std::vector<TreeHash>& children) const {
    if (children.empty()) {
        return TreeHash{};
    }

    // Hash through the fcmp++ crate, which alternates Selene <-> Helios. The
    // previous implementation reduced each child's compressed bytes modulo the
    // ed25519 group order and Pedersen-hashed the result: not injective, and
    // not openable inside the proving circuit, so proofs over it proved nothing.
    auto h = HashBranch(children);
    if (!h) return TreeHash{};
    return *h;
}

std::vector<TreeHash> CurveTree::GetChildren(const TreeIndex& parent) const {
    std::vector<TreeHash> children;

    if (parent.layer == 1) {
        // Layer 1 - children are leaf commitments (layer 0 nodes)
        uint64_t num_leaf_commits = (m_output_count + TreeConfig::LEAF_BRANCH_WIDTH - 1) /
                                     TreeConfig::LEAF_BRANCH_WIDTH;
        uint64_t start = parent.index * TreeConfig::INTERNAL_BRANCH_WIDTH;
        uint64_t end = std::min(start + TreeConfig::INTERNAL_BRANCH_WIDTH, num_leaf_commits);

        for (uint64_t i = start; i < end; ++i) {
            auto node = m_storage->GetNode(TreeIndex(0, i));
            if (node) {
                children.push_back(node->hash);
            }
        }
    } else if (parent.layer > 1) {
        // Internal layer - children are other nodes
        uint64_t start = parent.index * TreeConfig::INTERNAL_BRANCH_WIDTH;
        uint64_t child_layer = parent.layer - 1;

        // Calculate max children at this level
        uint64_t max_nodes_at_child_layer;
        if (child_layer == 0) {
            max_nodes_at_child_layer = (m_output_count + TreeConfig::LEAF_BRANCH_WIDTH - 1) /
                                        TreeConfig::LEAF_BRANCH_WIDTH;
        } else {
            // Recursively calculate
            uint64_t nodes = (m_output_count + TreeConfig::LEAF_BRANCH_WIDTH - 1) /
                             TreeConfig::LEAF_BRANCH_WIDTH;
            for (uint32_t l = 1; l <= child_layer; ++l) {
                nodes = (nodes + TreeConfig::INTERNAL_BRANCH_WIDTH - 1) /
                        TreeConfig::INTERNAL_BRANCH_WIDTH;
            }
            max_nodes_at_child_layer = nodes;
        }

        uint64_t end = std::min(start + TreeConfig::INTERNAL_BRANCH_WIDTH, max_nodes_at_child_layer);

        for (uint64_t i = start; i < end; ++i) {
            auto node = m_storage->GetNode(TreeIndex(child_layer, i));
            if (node) {
                children.push_back(node->hash);
            }
        }
    }

    return children;
}

TreeHash CurveTree::ComputeLeafNode(uint64_t leaf_index) const {
    // Hash this leaf branch through the audited fcmp++ leaf hash, which turns
    // each output into six Selene scalars (x and y of O, I and C) -- both
    // coordinates, unlike internal layers which use x only.
    uint64_t start = leaf_index * TreeConfig::LEAF_BRANCH_WIDTH;
    uint64_t end = std::min(start + TreeConfig::LEAF_BRANCH_WIDTH, m_output_count);

    std::vector<OutputTuple> outs;
    outs.reserve(end - start);
    for (uint64_t i = start; i < end; ++i) {
        auto output = m_storage->GetOutput(i);
        if (output) outs.push_back(*output);
    }

    auto h = HashLeafBranch(outs);
    if (!h) {
        // No usable leaf: return an explicitly empty node rather than a hash of
        // nothing that would be indistinguishable from a real one.
        return TreeHash{};
    }
    return *h;
}

void CurveTree::UpdatePath(uint64_t leaf_index) {
    // Start from the leaf commitment (layer 0)
    uint64_t leaf_commit_index = leaf_index / TreeConfig::LEAF_BRANCH_WIDTH;

    // Update layer 0 (leaf commitment)
    {
        TreeIndex idx(0, leaf_commit_index);
        TreeHash hash = ComputeLeafNode(leaf_commit_index);
        // Count outputs in this leaf
        uint64_t start = leaf_commit_index * TreeConfig::LEAF_BRANCH_WIDTH;
        uint64_t end = std::min(start + TreeConfig::LEAF_BRANCH_WIDTH, m_output_count);
        m_storage->StoreNode(idx, TreeNode(hash, end - start));
    }

    // Update internal layers (layer 1 through m_depth - 1)
    // Only if we have more than one layer
    if (m_depth > 1) {
        uint64_t current_index = leaf_commit_index;
        for (uint32_t layer = 1; layer < m_depth; ++layer) {
            uint64_t parent_index = current_index / TreeConfig::INTERNAL_BRANCH_WIDTH;
            TreeIndex parent_idx(layer, parent_index);

            auto children = GetChildren(parent_idx);
            if (!children.empty()) {
                TreeHash hash = ComputeNodeHash(children);
                m_storage->StoreNode(parent_idx, TreeNode(hash, children.size()));
            }

            current_index = parent_index;
        }
    }
}

std::optional<TreeBranch> CurveTree::GetBranch(uint64_t leaf_index) const {
    if (leaf_index >= m_output_count) {
        return std::nullopt;
    }

    TreeBranch branch;
    branch.leaf_index = leaf_index;

    // Layer 0: sibling outputs in same leaf commitment
    uint64_t leaf_commit_index = leaf_index / TreeConfig::LEAF_BRANCH_WIDTH;
    uint64_t start = leaf_commit_index * TreeConfig::LEAF_BRANCH_WIDTH;
    uint64_t end = std::min(start + TreeConfig::LEAF_BRANCH_WIDTH, m_output_count);

    // The leaf branch is carried as OUTPUTS, not as flattened scalars.
    // fcmp_prove_full rebuilds the leaf hash itself and needs the outputs plus
    // our position among them; scalars alone cannot be turned back into points.
    for (uint64_t i = start; i < end; ++i) {
        auto output = m_storage->GetOutput(i);
        if (output) branch.leaves.push_back(*output);
    }
    branch.index_in_leaves = leaf_index - start;

    // Internal layers
    uint64_t current_index = leaf_commit_index;
    for (uint32_t layer = 1; layer < m_depth; ++layer) {
        uint64_t parent_index = current_index / TreeConfig::INTERNAL_BRANCH_WIDTH;
        uint64_t sibling_start = parent_index * TreeConfig::INTERNAL_BRANCH_WIDTH;

        // Carry the siblings as curve-tagged hashes. Reducing them to ed25519
        // scalars (as before) discarded the curve and produced values the
        // prover cannot use.
        auto children = GetChildren(TreeIndex(layer, parent_index));
        branch.layers.push_back(std::move(children));

        current_index = parent_index;
    }

    return branch;
}

bool CurveTree::VerifyBranch(const OutputTuple& output, const TreeBranch& branch,
                             const TreeHash& expected_root) {
    // A branch with no leaves names no output.
    if (branch.leaves.empty()) {
        return false;
    }

    // Recompute the branch through the real curve-tree hashing rather than the
    // old ed25519 Pedersen hash, which could not be opened in the proving
    // circuit and so verified nothing meaningful.
    auto leaf = HashLeafBranch(branch.leaves);
    if (!leaf) return false;
    TreeHash current = *leaf;

    for (size_t layer = 0; layer < branch.layers.size(); ++layer) {
        auto h = HashBranch(branch.layers[layer]);
        if (!h) return false;
        current = *h;
    }

    return current == expected_root;
}

bool CurveTree::Rebuild() {
    if (m_output_count == 0) {
        return true;
    }

    m_storage->BeginBatch();

    // Rebuild leaf layer (layer 0) - these are commitments to groups of outputs
    uint64_t num_leaf_commits = (m_output_count + TreeConfig::LEAF_BRANCH_WIDTH - 1) /
                                 TreeConfig::LEAF_BRANCH_WIDTH;

    for (uint64_t i = 0; i < num_leaf_commits; ++i) {
        TreeHash hash = ComputeLeafNode(i);
        uint64_t start = i * TreeConfig::LEAF_BRANCH_WIDTH;
        uint64_t end = std::min(start + TreeConfig::LEAF_BRANCH_WIDTH, m_output_count);
        m_storage->StoreNode(TreeIndex(0, i), TreeNode(hash, end - start));
    }

    // Rebuild internal layers (layer 1 and above)
    uint64_t nodes_at_prev_layer = num_leaf_commits;
    for (uint32_t layer = 1; layer < m_depth; ++layer) {
        uint64_t nodes_at_layer = (nodes_at_prev_layer + TreeConfig::INTERNAL_BRANCH_WIDTH - 1) /
                                   TreeConfig::INTERNAL_BRANCH_WIDTH;

        for (uint64_t i = 0; i < nodes_at_layer; ++i) {
            auto children = GetChildren(TreeIndex(layer, i));
            if (!children.empty()) {
                TreeHash hash = ComputeNodeHash(children);
                m_storage->StoreNode(TreeIndex(layer, i), TreeNode(hash, children.size()));
            }
        }

        nodes_at_prev_layer = nodes_at_layer;
    }

    m_storage->CommitBatch();
    m_root_dirty = true;

    return true;
}

bool CurveTree::VerifyIntegrity() const {
    if (m_output_count == 0) {
        return true;
    }

    // Verify each layer's hashes
    uint64_t num_leaf_commits = (m_output_count + TreeConfig::LEAF_BRANCH_WIDTH - 1) /
                                 TreeConfig::LEAF_BRANCH_WIDTH;

    // Check leaf layer (layer 0) - these are commitments to groups of outputs
    for (uint64_t i = 0; i < num_leaf_commits; ++i) {
        auto stored = m_storage->GetNode(TreeIndex(0, i));
        if (!stored) {
            return false;
        }

        TreeHash expected = ComputeLeafNode(i);
        if (stored->hash != expected) {
            return false;
        }
    }

    // Check internal layers (layer 1 and above)
    uint64_t nodes_at_prev_layer = num_leaf_commits;
    for (uint32_t layer = 1; layer < m_depth; ++layer) {
        uint64_t nodes_at_layer = (nodes_at_prev_layer + TreeConfig::INTERNAL_BRANCH_WIDTH - 1) /
                                   TreeConfig::INTERNAL_BRANCH_WIDTH;

        for (uint64_t i = 0; i < nodes_at_layer; ++i) {
            auto stored = m_storage->GetNode(TreeIndex(layer, i));
            if (!stored) {
                return false;
            }

            auto children = GetChildren(TreeIndex(layer, i));
            TreeHash expected = ComputeNodeHash(children);
            if (stored->hash != expected) {
                return false;
            }
        }

        nodes_at_prev_layer = nodes_at_layer;
    }

    return true;
}

bool CurveTree::Save() {
    // Save output count
    std::vector<uint8_t> count_data(8);
    for (int i = 0; i < 8; ++i) {
        count_data[i] = (m_output_count >> (i * 8)) & 0xFF;
    }
    m_storage->StoreMetadata("output_count", count_data);

    // Save depth
    std::vector<uint8_t> depth_data(4);
    for (int i = 0; i < 4; ++i) {
        depth_data[i] = (m_depth >> (i * 8)) & 0xFF;
    }
    m_storage->StoreMetadata("depth", depth_data);

    return true;
}

bool CurveTree::Load() {
    // Load output count
    auto count_data = m_storage->GetMetadata("output_count");
    if (count_data && count_data->size() == 8) {
        m_output_count = 0;
        for (int i = 0; i < 8; ++i) {
            m_output_count |= static_cast<uint64_t>((*count_data)[i]) << (i * 8);
        }
    } else {
        m_output_count = m_storage->GetOutputCount();
    }

    // Load or calculate depth
    auto depth_data = m_storage->GetMetadata("depth");
    if (depth_data && depth_data->size() == 4) {
        m_depth = 0;
        for (int i = 0; i < 4; ++i) {
            m_depth |= static_cast<uint32_t>((*depth_data)[i]) << (i * 8);
        }
    } else {
        m_depth = CalculateDepth(m_output_count);
    }

    m_root_dirty = true;
    return true;
}

// ============================================================================
// CurveTreeBuilder Implementation
// ============================================================================

CurveTreeBuilder::CurveTreeBuilder(std::shared_ptr<ITreeStorage> storage)
    : m_storage(std::move(storage))
{
}

void CurveTreeBuilder::AddOutputs(const std::vector<OutputTuple>& outputs) {
    m_outputs.insert(m_outputs.end(), outputs.begin(), outputs.end());

    if (m_progress_cb) {
        m_progress_cb(m_outputs.size(), m_outputs.size());
    }
}

std::unique_ptr<CurveTree> CurveTreeBuilder::Finalize() {
    auto tree = std::make_unique<CurveTree>(m_storage);

    // Add all outputs
    tree->AddOutputs(m_outputs);

    // Clear builder state
    m_outputs.clear();

    return tree;
}

} // namespace curvetree
