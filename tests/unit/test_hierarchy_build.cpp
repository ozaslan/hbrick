#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "hbrick/core/types.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hbrick_index.hpp"
#include "hbrick/tile/super_tile_oracle.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

hbrick::TileSlot tileSlotFromRegion(const hbrick::RegionNode& region) {
    hbrick::TileSlot slot{};
    slot.tile_i = region.region_i;
    slot.tile_j = region.region_j;
    slot.origin = region.origin;
    slot.extent = region.extent;
    return slot;
}

hbrick::HBrickConfig openConfig(
    const hbrick::TileSize base_tile_size,
    const hbrick::GroupSize group_size,
    const uint32_t max_depth
) {
    hbrick::HBrickConfig config{};
    config.base_tile_size = base_tile_size;
    config.group_size = group_size;
    config.max_depth = max_depth;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();
    return config;
}

}  // namespace

TEST(HierarchyBuild, CompletesOnEightByEightWithOneSuperLevel) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const hbrick::HBrickIndex index = hbrick::HBrickIndex::build(
        graph,
        layout,
        openConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 2U)
    );

    EXPECT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    EXPECT_EQ(index.hierarchy().numLevels(), 2U);
    EXPECT_TRUE(index.hasSuperLevel(1U));
    EXPECT_EQ(index.superLevel(1U).size(), 1U);
    EXPECT_FALSE(index.hasSuperLevel(2U));
}

TEST(HierarchyBuild, MaxDepthOneStoresOnlyBaseTiles) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const hbrick::HBrickIndex index = hbrick::HBrickIndex::build(
        graph,
        layout,
        openConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 1U)
    );

    EXPECT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    EXPECT_EQ(index.hierarchy().numLevels(), 1U);
    EXPECT_FALSE(index.hasSuperLevel(1U));
    EXPECT_EQ(index.brickIndex().tiles().decomposition().numSlots(), 4U);
}

TEST(HierarchyBuild, RootSummaryMatchesFlatPortOracle) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{3U, 3U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{23U, 0.6, 0.4, 0.0, 0.0}
    );

    const hbrick::HBrickIndex index = hbrick::HBrickIndex::build(
        graph,
        layout,
        openConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 2U)
    );
    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);

    const hbrick::RegionNode& root = index.hierarchy().node(1U, 0U);
    const hbrick::SuperTileSummary& root_summary = index.superSummary(1U, 0U);
    const hbrick::TileSlot parent_slot = tileSlotFromRegion(root);

    const hbrick::BitMatrix flat_boundary = hbrick::buildFlatRegionPortBoundarySummary(
        index.brickIndex(),
        parent_slot,
        root_summary.gamma,
        std::numeric_limits<uint64_t>::max()
    );

    EXPECT_TRUE(hbrick::bitMatricesEqual(root_summary.boundary_summary, flat_boundary));
}

TEST(HierarchyBuild, FullDepthBuildsMultiLevelHierarchyOnSixteenBySixteen) {
    hbrick::MazeLayout layout(16U, 16U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const hbrick::HBrickIndex index = hbrick::HBrickIndex::build(
        graph,
        layout,
        openConfig(
            hbrick::TileSize{4U, 4U},
            hbrick::GroupSize{2U, 2U},
            hbrick::kHBrickFullDepth
        )
    );

    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    EXPECT_EQ(index.hierarchy().numLevels(), 3U);
    EXPECT_EQ(index.superLevel(1U).size(), 4U);
    EXPECT_EQ(index.superLevel(2U).size(), 1U);

    const hbrick::RegionNode& root = index.hierarchy().node(2U, 0U);
    const hbrick::SuperTileSummary& root_summary = index.superSummary(2U, 0U);
    EXPECT_EQ(root.origin.x, 0U);
    EXPECT_EQ(root.origin.y, 0U);
    EXPECT_EQ(root.extent.width, 16U);
    EXPECT_EQ(root.extent.height, 16U);
    EXPECT_EQ(root_summary.slot.extent.width, 16U);
    EXPECT_EQ(root_summary.slot.extent.height, 16U);

    const hbrick::RegionCellClosure region_closure = hbrick::buildRegionCellClosure(
        graph,
        layout,
        tileSlotFromRegion(root),
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(region_closure.status, hbrick::BaselineStatus::Completed);
    EXPECT_TRUE(hbrick::boundarySummaryMatchesCellClosure(
        region_closure,
        root_summary.exterior_ports,
        root_summary.boundary_summary
    ));
}
