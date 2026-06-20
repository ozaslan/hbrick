#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/tile/external_boundary_ports.hpp"
#include "hbrick/tile/group_tile_slots.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/tile_boundary_order.hpp"
#include "hbrick/tile/tile_decomposition.hpp"

TEST(HierarchyScaffold, GroupTileSlotsPartitionsSlotGrid) {
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{4U, 4U});

    const std::vector<hbrick::SlotGroup> groups =
        hbrick::groupTileSlots(decomposition, hbrick::GroupSize{2U, 2U});

    ASSERT_EQ(groups.size(), 1U);
    EXPECT_EQ(groups.front().region_i, 0U);
    EXPECT_EQ(groups.front().region_j, 0U);
    EXPECT_EQ(groups.front().child_slot_i_begin, 0U);
    EXPECT_EQ(groups.front().child_slot_j_begin, 0U);
    EXPECT_EQ(groups.front().child_slot_i_end, 2U);
    EXPECT_EQ(groups.front().child_slot_j_end, 2U);
}

TEST(HierarchyScaffold, GroupTileSlotsAllowsPartialEdgeGroups) {
    const std::vector<hbrick::SlotGroup> groups =
        hbrick::groupTileSlots(3U, 3U, hbrick::GroupSize{2U, 2U});

    ASSERT_EQ(groups.size(), 4U);
    EXPECT_EQ(groups.back().child_slot_i_begin, 2U);
    EXPECT_EQ(groups.back().child_slot_j_begin, 2U);
    EXPECT_EQ(groups.back().child_slot_i_end, 3U);
    EXPECT_EQ(groups.back().child_slot_j_end, 3U);
}

TEST(HierarchyScaffold, ExternalBoundaryPortsMatchesCanonicalOrder) {
    const hbrick::TileSlot slot{
        0U,
        0U,
        hbrick::GridCoord{1U, 2U},
        hbrick::GridDimensions{4U, 3U},
    };

    const std::vector<hbrick::GridCoord> ports = hbrick::externalBoundaryPorts(slot);
    const std::vector<hbrick::GridCoord> canonical = hbrick::canonicalBoundaryCoords(slot);

    ASSERT_EQ(ports.size(), canonical.size());
    for (std::size_t index = 0U; index < ports.size(); ++index) {
        EXPECT_EQ(ports[index].x, canonical[index].x);
        EXPECT_EQ(ports[index].y, canonical[index].y);
    }
}

TEST(HierarchyScaffold, HierarchyTreeBuildsParentLinks) {
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{4U, 4U});

    const hbrick::HierarchyTree tree = hbrick::HierarchyTree::build(
        decomposition,
        hbrick::GroupSize{2U, 2U},
        3U
    );

    EXPECT_EQ(tree.numLevels(), 2U);
    EXPECT_EQ(tree.level(0U).size(), 4U);
    EXPECT_EQ(tree.level(1U).size(), 1U);

    const hbrick::RegionNode& root = tree.node(1U, 0U);
    EXPECT_EQ(root.children.size(), 4U);
    EXPECT_EQ(root.origin.x, 0U);
    EXPECT_EQ(root.origin.y, 0U);
    EXPECT_EQ(root.extent.width, 8U);
    EXPECT_EQ(root.extent.height, 8U);

    for (const hbrick::RegionNodeId& child_id : root.children) {
        const hbrick::RegionNode& child = tree.node(child_id.level, child_id.index);
        EXPECT_TRUE(child.has_parent);
        EXPECT_EQ(child.parent.level, 1U);
        EXPECT_EQ(child.parent.index, 0U);
    }
}

TEST(HierarchyScaffold, HierarchyTreeRootCoversFullMap) {
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(9U, 7U, hbrick::TileSize{4U, 4U});

    const hbrick::HierarchyTree tree = hbrick::HierarchyTree::build(
        decomposition,
        hbrick::GroupSize{2U, 2U},
        8U
    );

    ASSERT_GE(tree.numLevels(), 2U);
    const std::span<const hbrick::RegionNode> top = tree.level(tree.numLevels() - 1U);
    ASSERT_EQ(top.size(), 1U);

    const hbrick::RegionNode& root = top.front();
    EXPECT_EQ(root.origin.x, 0U);
    EXPECT_EQ(root.origin.y, 0U);
    EXPECT_EQ(root.extent.width, decomposition.mapWidth());
    EXPECT_EQ(root.extent.height, decomposition.mapHeight());
}

TEST(HierarchyScaffold, HierarchyTreeChildrenPartitionBaseLevel) {
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{4U, 4U});

    const hbrick::HierarchyTree tree = hbrick::HierarchyTree::build(
        decomposition,
        hbrick::GroupSize{2U, 1U},
        2U
    );

    ASSERT_EQ(tree.numLevels(), 2U);
    EXPECT_EQ(tree.level(1U).size(), 2U);

    std::set<std::string> covered_children;
    for (const hbrick::RegionNode& parent : tree.level(1U)) {
        for (const hbrick::RegionNodeId& child_id : parent.children) {
            const auto insert_result =
                covered_children.insert(std::to_string(child_id.index));
            EXPECT_TRUE(insert_result.second);
        }
    }

    EXPECT_EQ(covered_children.size(), tree.level(0U).size());
}
