#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "hbrick/core/types.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/super_tile_composer.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

hbrick::TileSlot parentSlotFromRegion(const hbrick::RegionNode& region) {
    hbrick::TileSlot slot{};
    slot.tile_i = region.region_i;
    slot.tile_j = region.region_j;
    slot.origin = region.origin;
    slot.extent = region.extent;
    return slot;
}

}  // namespace

TEST(SuperTileCompose, ComposesFourBaseTilesIntoOneParent) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const hbrick::BrickIndex brick_index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max(),
        true
    );
    ASSERT_EQ(brick_index.status(), hbrick::BaselineStatus::Completed);

    const hbrick::TileDecomposition decomposition =
        brick_index.tiles().decomposition();
    const hbrick::HierarchyTree tree = hbrick::HierarchyTree::build(
        decomposition,
        hbrick::GroupSize{2U, 2U},
        2U
    );
    ASSERT_EQ(tree.numLevels(), 2U);
    ASSERT_EQ(tree.level(1U).size(), 1U);

    const hbrick::RegionNode& parent_region = tree.node(1U, 0U);
    const hbrick::TileSlot parent_slot = parentSlotFromRegion(parent_region);

    std::vector<hbrick::ChildBoundarySummary> children;
    std::vector<std::vector<hbrick::GridCoord>> child_port_storage;
    child_port_storage.reserve(parent_region.children.size());
    children.reserve(parent_region.children.size());

    for (const hbrick::RegionNodeId& child_id : parent_region.children) {
        child_port_storage.emplace_back();
        const hbrick::BaseTileSummary& summary =
            brick_index.tiles().summaryByIndex(child_id.index);
        children.push_back(hbrick::childBoundaryFromBaseTile(
            summary,
            child_id.index,
            child_port_storage.back()
        ));
    }

    const hbrick::SuperTileSummary composed = hbrick::composeSuperTile(
        parent_slot,
        children,
        brick_index.ports(),
        brick_index.seamEdges(),
        std::numeric_limits<uint64_t>::max()
    );

    EXPECT_EQ(composed.status, hbrick::BaselineStatus::Completed);
    EXPECT_GT(composed.gamma.ports.size(), composed.exterior_ports.size());
    EXPECT_EQ(
        composed.boundary_summary.numRows(),
        static_cast<uint32_t>(composed.exterior_ports.size())
    );
    EXPECT_EQ(composed.boundary_summary.numRows(), composed.boundary_summary.numCols());
    EXPECT_EQ(composed.child_embeddings.size(), children.size());

    for (uint32_t index = 0U; index < composed.boundary_summary.numRows(); ++index) {
        EXPECT_TRUE(composed.boundary_summary.test(index, index));
    }
}

TEST(SuperTileCompose, SkipsImpassableChildrenWithEmptyPorts) {
    hbrick::MazeLayout layout(16U, 16U, true);
    for (uint32_t y = 0U; y < 8U; ++y) {
        for (uint32_t x = 0U; x < 8U; ++x) {
            layout.setPassable(x, y, false);
        }
    }

    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const hbrick::BrickIndex brick_index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{8U, 8U},
        std::numeric_limits<uint64_t>::max(),
        true
    );
    ASSERT_EQ(brick_index.status(), hbrick::BaselineStatus::Completed);

    const hbrick::TileDecomposition decomposition =
        brick_index.tiles().decomposition();
    const hbrick::HierarchyTree tree = hbrick::HierarchyTree::build(
        decomposition,
        hbrick::GroupSize{2U, 2U},
        2U
    );
    ASSERT_EQ(tree.numLevels(), 2U);

    const hbrick::RegionNode& parent_region = tree.node(1U, 0U);
    const hbrick::TileSlot parent_slot = parentSlotFromRegion(parent_region);

    std::vector<hbrick::ChildBoundarySummary> children;
    std::vector<std::vector<hbrick::GridCoord>> child_port_storage;
    child_port_storage.reserve(parent_region.children.size());
    children.reserve(parent_region.children.size());

    for (const hbrick::RegionNodeId& child_id : parent_region.children) {
        child_port_storage.emplace_back();
        const hbrick::BaseTileSummary& summary =
            brick_index.tiles().summaryByIndex(child_id.index);
        children.push_back(hbrick::childBoundaryFromBaseTile(
            summary,
            child_id.index,
            child_port_storage.back()
        ));
    }

    const hbrick::SuperTileSummary composed = hbrick::composeSuperTile(
        parent_slot,
        children,
        brick_index.ports(),
        brick_index.seamEdges(),
        std::numeric_limits<uint64_t>::max()
    );

    EXPECT_EQ(composed.status, hbrick::BaselineStatus::Completed);
    EXPECT_FALSE(composed.gamma.ports.empty());
    EXPECT_LT(composed.child_embeddings.size(), children.size());
}

TEST(SuperTileCompose, GammaOrderingIsDeterministic) {
    const hbrick::TileSlot parent{
        0U,
        0U,
        hbrick::GridCoord{0U, 0U},
        hbrick::GridDimensions{8U, 8U},
    };

    const std::vector<hbrick::GridCoord> child_a_ports{
        hbrick::GridCoord{3U, 0U},
        hbrick::GridCoord{0U, 3U},
    };
    const std::vector<hbrick::GridCoord> child_b_ports{
        hbrick::GridCoord{4U, 0U},
        hbrick::GridCoord{7U, 3U},
    };

    hbrick::BitMatrix summary_a(2U, 2U);
    hbrick::BitMatrix summary_b(2U, 2U);
    summary_a.set(0U, 0U);
    summary_a.set(1U, 1U);
    summary_b.set(0U, 0U);
    summary_b.set(1U, 1U);

    const hbrick::ChildBoundarySummary child_a{
        parent,
        child_a_ports,
        &summary_a,
        0U,
    };
    const hbrick::ChildBoundarySummary child_b{
        parent,
        child_b_ports,
        &summary_b,
        1U,
    };
    const hbrick::ChildBoundarySummary child_inputs[] = {child_a, child_b};

    const hbrick::GammaOrdering gamma =
        hbrick::buildGammaOrdering(parent, child_inputs);

    ASSERT_EQ(gamma.ports.size(), 4U);
    EXPECT_EQ(gamma.ports[0U].x, 3U);
    EXPECT_EQ(gamma.ports[0U].y, 0U);
    EXPECT_EQ(gamma.ports[1U].x, 4U);
    EXPECT_EQ(gamma.ports[2U].x, 7U);
    EXPECT_EQ(gamma.ports[3U].x, 0U);
    EXPECT_EQ(gamma.ports[3U].y, 3U);
}
