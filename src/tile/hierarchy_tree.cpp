#include "hbrick/tile/hierarchy_tree.hpp"

#include <algorithm>
#include <stdexcept>

#include "hbrick/tile/group_tile_slots.hpp"

namespace hbrick {

namespace {

[[nodiscard]] TileSlot unionRegionBbox(const std::span<const RegionNode> children) {
    TileSlot bbox{};
    bbox.origin = children.front().origin;
    bbox.extent = children.front().extent;

    for (const RegionNode& child : children) {
        const uint32_t max_x = std::max(bbox.maxX(), child.origin.x + child.extent.width);
        const uint32_t max_y = std::max(bbox.maxY(), child.origin.y + child.extent.height);
        bbox.origin.x = std::min(bbox.origin.x, child.origin.x);
        bbox.origin.y = std::min(bbox.origin.y, child.origin.y);
        bbox.extent.width = max_x - bbox.origin.x;
        bbox.extent.height = max_y - bbox.origin.y;
    }

    return bbox;
}

[[nodiscard]] std::vector<RegionNode> buildBaseLevel(
    const TileDecomposition& decomposition
) {
    std::vector<RegionNode> nodes;
    nodes.reserve(decomposition.numSlots());

    for (uint32_t index = 0U; index < decomposition.numSlots(); ++index) {
        const TileSlot& slot = decomposition.slotByIndex(index);
        RegionNode node{};
        node.id = RegionNodeId{0U, index};
        node.has_parent = false;
        node.region_i = slot.tile_i;
        node.region_j = slot.tile_j;
        node.origin = slot.origin;
        node.extent = slot.extent;
        nodes.push_back(node);
    }

    return nodes;
}

[[nodiscard]] uint32_t childGridWidth(const std::span<const RegionNode> child_level) {
    uint32_t width = 0U;
    for (const RegionNode& node : child_level) {
        width = std::max(width, node.region_i + 1U);
    }
    return width;
}

[[nodiscard]] uint32_t childGridHeight(const std::span<const RegionNode> child_level) {
    uint32_t height = 0U;
    for (const RegionNode& node : child_level) {
        height = std::max(height, node.region_j + 1U);
    }
    return height;
}

[[nodiscard]] const RegionNode& childAt(
    const std::span<const RegionNode> child_level,
    const uint32_t child_num_i,
    const uint32_t tile_i,
    const uint32_t tile_j
) {
    const uint32_t child_index = tile_j * child_num_i + tile_i;
    return child_level[child_index];
}

[[nodiscard]] std::vector<RegionNode> buildGroupedLevel(
    const std::span<const RegionNode> child_level,
    const GroupSize group_size,
    const uint32_t new_level
) {
    const uint32_t child_num_i = childGridWidth(child_level);
    const uint32_t child_num_j = childGridHeight(child_level);
    const std::vector<SlotGroup> groups =
        groupTileSlots(child_num_i, child_num_j, group_size);

    std::vector<RegionNode> nodes;
    nodes.reserve(groups.size());

    for (uint32_t group_index = 0U; group_index < groups.size(); ++group_index) {
        const SlotGroup& group = groups[group_index];

        std::vector<RegionNode> children;
        children.reserve(
            static_cast<std::size_t>(group.child_slot_i_end - group.child_slot_i_begin) *
            static_cast<std::size_t>(group.child_slot_j_end - group.child_slot_j_begin)
        );

        for (uint32_t tile_j = group.child_slot_j_begin; tile_j < group.child_slot_j_end;
             ++tile_j) {
            for (uint32_t tile_i = group.child_slot_i_begin; tile_i < group.child_slot_i_end;
                 ++tile_i) {
                children.push_back(childAt(child_level, child_num_i, tile_i, tile_j));
            }
        }

        const TileSlot bbox = unionRegionBbox(children);

        RegionNode node{};
        node.id = RegionNodeId{new_level, group_index};
        node.has_parent = false;
        node.region_i = group.region_i;
        node.region_j = group.region_j;
        node.origin = bbox.origin;
        node.extent = bbox.extent;
        node.children.reserve(children.size());
        for (const RegionNode& child : children) {
            node.children.push_back(child.id);
        }
        nodes.push_back(node);
    }

    return nodes;
}

}  // namespace

HierarchyTree HierarchyTree::build(
    const TileDecomposition& base_decomposition,
    const GroupSize group_size,
    const uint32_t max_depth
) {
    if (!group_size.isValid()) {
        throw std::invalid_argument("HierarchyTree::build: invalid group size");
    }
    if (max_depth == 0U) {
        throw std::invalid_argument("HierarchyTree::build: max_depth must be at least 1");
    }

    HierarchyTree tree;
    tree.base_decomposition_ = base_decomposition;
    tree.group_size_ = group_size;
    tree.levels_.push_back(buildBaseLevel(base_decomposition));

    uint32_t depth = 1U;
    while (depth < max_depth) {
        const std::vector<RegionNode>& previous = tree.levels_.back();
        if (previous.size() <= 1U) {
            break;
        }

        std::vector<RegionNode> grouped =
            buildGroupedLevel(previous, group_size, depth);
        if (grouped.empty() || grouped.size() >= previous.size()) {
            break;
        }

        for (RegionNode& child : tree.levels_.back()) {
            child.has_parent = true;
        }

        for (std::size_t parent_index = 0U; parent_index < grouped.size(); ++parent_index) {
            RegionNode& parent = grouped[parent_index];
            parent.id.index = static_cast<uint32_t>(parent_index);
            for (const RegionNodeId& child_id : parent.children) {
                tree.levels_.back()[child_id.index].parent =
                    RegionNodeId{depth, static_cast<uint32_t>(parent_index)};
            }
        }

        tree.levels_.push_back(std::move(grouped));
        ++depth;
    }

    return tree;
}

std::span<const RegionNode> HierarchyTree::level(const uint32_t level_index) const noexcept {
    if (level_index >= levels_.size()) {
        return {};
    }
    return levels_[level_index];
}

const RegionNode& HierarchyTree::node(
    const uint32_t level_index,
    const uint32_t node_index
) const noexcept {
    return levels_[level_index][node_index];
}

}  // namespace hbrick
