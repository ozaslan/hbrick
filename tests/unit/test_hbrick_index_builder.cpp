#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_build_format.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hbrick_index_builder.hpp"

namespace {

hbrick::HBrickConfig openConfig() {
    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{4U, 4U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    config.max_depth = 2U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();
    return config;
}

}  // namespace

TEST(HBrickIndexBuilder, CompletesWithProgressAndReport) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::HBrickIndexBuilder builder;
    builder.begin(graph, layout, openConfig());
    ASSERT_TRUE(builder.running());

    while (!builder.step()) {
    }

    const hbrick::HBrickBuildReport& report = builder.report();
    EXPECT_TRUE(report.valid);
    EXPECT_EQ(report.status, hbrick::BaselineStatus::Completed);
    EXPECT_GT(report.total_nanoseconds, 0U);
    EXPECT_EQ(report.num_base_tiles, 4U);
    EXPECT_EQ(report.num_base_with_closure, 4U);
    EXPECT_GT(report.base_tile_closure_nanoseconds, 0U);
    EXPECT_LE(report.base_tile_closure_nanoseconds, report.base_tile_nanoseconds);
    EXPECT_EQ(report.num_hierarchy_levels, 2U);
    EXPECT_EQ(report.super_levels.size(), 1U);
    EXPECT_EQ(report.super_levels.front().num_completed, 1U);
    EXPECT_GT(report.estimated_storage_bytes, 0U);
    EXPECT_GT(report.estimated_brick_storage_bytes, 0U);
    EXPECT_EQ(
        report.estimated_storage_bytes,
        report.estimated_brick_storage_bytes + report.estimated_hbrick_extra_storage_bytes
    );
    ASSERT_GE(report.level_graph_stats.size(), 1U);
    EXPECT_EQ(report.level_graph_stats.front().level, 0U);
    EXPECT_EQ(report.level_graph_stats.front().num_graphs, 1U);
    EXPECT_GT(report.level_graph_stats.front().total_nodes, 0U);

    const hbrick::HBrickIndex index = builder.takeIndex();
    EXPECT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    EXPECT_TRUE(index.hasSuperLevel(1U));
}

TEST(HBrickIndexBuilder, CompletesWhenSuperRegionHasImpassableChildren) {
    hbrick::MazeLayout layout(24U, 24U, true);
    for (uint32_t y = 0U; y < 8U; ++y) {
        for (uint32_t x = 0U; x < 8U; ++x) {
            layout.setPassable(x, y, false);
        }
    }

    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{8U, 8U};
    config.group_size = hbrick::GroupSize{3U, 3U};
    config.max_depth = 2U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickIndexBuilder builder;
    builder.begin(graph, layout, config);
    ASSERT_TRUE(builder.running());

    while (!builder.step()) {
    }

    const hbrick::HBrickBuildReport& report = builder.report();
    EXPECT_TRUE(report.valid);
    EXPECT_EQ(report.status, hbrick::BaselineStatus::Completed);
    EXPECT_EQ(report.super_levels.size(), 1U);
    EXPECT_EQ(report.super_levels.front().num_completed, 1U);

    const hbrick::HBrickIndex index = builder.takeIndex();
    EXPECT_EQ(index.status(), hbrick::BaselineStatus::Completed);
}

TEST(HBrickIndexBuilder, ProgressDetailFormatsBaseTileStage) {
    hbrick::HBrickBuildProgress progress{};
    progress.stage = hbrick::HBrickBuildStage::BaseTiles;
    progress.current_base_tile_index = 1U;
    progress.num_base_tiles = 4U;

    char buffer[64];
    hbrick::formatHBrickBuildProgressDetail(buffer, sizeof(buffer), progress);
    EXPECT_NE(std::string(buffer).find("2 / 4"), std::string::npos);
}

TEST(HBrickIndexBuilder, EnforcesMemoryBudgetAgainstBaseAndSuperTiles) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    // Run with unlimited budget first to get exact base BRICK storage bytes
    hbrick::HBrickIndexBuilder unconstrained_builder;
    unconstrained_builder.begin(graph, layout, openConfig());
    while (!unconstrained_builder.step()) {}
    ASSERT_EQ(unconstrained_builder.report().status, hbrick::BaselineStatus::Completed);
    const uint64_t brick_storage = unconstrained_builder.report().estimated_brick_storage_bytes;
    const uint64_t total_storage = unconstrained_builder.report().estimated_storage_bytes;
    ASSERT_GT(brick_storage, 0U);
    ASSERT_GT(total_storage, brick_storage);

    // Set budget such that base BRICK fits, but base + super-tiles exceeds budget
    hbrick::HBrickConfig tight_config = openConfig();
    tight_config.max_memory_bytes = brick_storage + 10U;

    hbrick::HBrickIndexBuilder constrained_builder;
    constrained_builder.begin(graph, layout, tight_config);
    while (!constrained_builder.step()) {}

    EXPECT_NE(constrained_builder.report().status, hbrick::BaselineStatus::Completed);
    EXPECT_TRUE(constrained_builder.report().status == hbrick::BaselineStatus::OutOfMemory
                || constrained_builder.report().status == hbrick::BaselineStatus::SkippedByPolicy);
}

TEST(HBrickIndexBuilder, MeasureStorageBytesReflectsRetainedAllocations) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::HBrickIndexBuilder builder;
    builder.begin(graph, layout, openConfig());
    while (!builder.step()) {}

    const hbrick::HBrickIndex index = builder.takeIndex();
    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    const uint64_t measured = index.measureStorageBytes();
    EXPECT_GT(measured, 0U);
    EXPECT_GT(measured, index.brickIndex().measureStorageBytes());
}

TEST(HBrickIndexBuilder, MultiLevelHierarchyThreeLevelsBuildsAndMeasures) {
    // 32x32 map, 4x4 base tiles (8x8 slots = 64 base tiles), 2x2 grouping -> 3 levels
    // Level 0: 64 base tiles (4x4)
    // Level 1: 16 super tiles (8x8)
    // Level 2: 4 super tiles (16x16)
    hbrick::MazeLayout layout(32U, 32U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{4U, 4U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    config.max_depth = 3U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickIndexBuilder builder;
    builder.begin(graph, layout, config);
    while (!builder.step()) {}

    const hbrick::HBrickBuildReport& report = builder.report();
    EXPECT_EQ(report.status, hbrick::BaselineStatus::Completed);
    EXPECT_EQ(report.num_hierarchy_levels, 3U);
    EXPECT_EQ(report.super_levels.size(), 2U);
    EXPECT_EQ(report.super_levels[0].num_completed, 16U);
    EXPECT_EQ(report.super_levels[1].num_completed, 4U);

    const hbrick::HBrickIndex index = builder.takeIndex();
    EXPECT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    EXPECT_TRUE(index.hasSuperLevel(1U));
    EXPECT_TRUE(index.hasSuperLevel(2U));
    EXPECT_FALSE(index.hasSuperLevel(3U));

    const uint64_t measured = index.measureStorageBytes();
    EXPECT_GT(measured, index.brickIndex().measureStorageBytes());
}

TEST(HBrickIndexBuilder, CompletelyImpassableMapProducesGracefulCompletedIndex) {
    // 8x8 map where ALL cells are impassable walls
    hbrick::MazeLayout layout(8U, 8U, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::HBrickIndexBuilder builder;
    builder.begin(graph, layout, openConfig());
    while (!builder.step()) {}

    const hbrick::HBrickBuildReport& report = builder.report();
    EXPECT_EQ(report.status, hbrick::BaselineStatus::Completed);
    EXPECT_EQ(report.num_base_with_closure, 0U);

    const hbrick::HBrickIndex index = builder.takeIndex();
    EXPECT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    EXPECT_EQ(index.brickIndex().ports().numPorts(), 0U);
    EXPECT_EQ(index.brickIndex().portGraph().numVertices(), 0U);
}

TEST(HBrickIndexBuilder, ExceedingBudgetAtHigherLevelSuperTileTriggersOutOfMemory) {
    // Determine the exact memory needed for 2-level build
    hbrick::MazeLayout layout(16U, 16U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::HBrickConfig config_depth2{};
    config_depth2.base_tile_size = hbrick::TileSize{4U, 4U};
    config_depth2.group_size = hbrick::GroupSize{2U, 2U};
    config_depth2.max_depth = 2U;
    config_depth2.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickIndexBuilder depth2_builder;
    depth2_builder.begin(graph, layout, config_depth2);
    while (!depth2_builder.step()) {}
    ASSERT_EQ(depth2_builder.report().status, hbrick::BaselineStatus::Completed);
    const uint64_t depth2_storage = depth2_builder.report().estimated_storage_bytes;

    // Run 3-level build to see how much more level 2 super tiles require
    hbrick::HBrickConfig config_depth3 = config_depth2;
    config_depth3.max_depth = 3U;

    hbrick::HBrickIndexBuilder depth3_builder;
    depth3_builder.begin(graph, layout, config_depth3);
    while (!depth3_builder.step()) {}
    ASSERT_EQ(depth3_builder.report().status, hbrick::BaselineStatus::Completed);
    const uint64_t depth3_storage = depth3_builder.report().estimated_storage_bytes;
    ASSERT_GT(depth3_storage, depth2_storage);

    // Set budget between depth2 storage and depth3 storage
    hbrick::HBrickConfig tight_config = config_depth3;
    tight_config.max_memory_bytes = depth2_storage + (depth3_storage - depth2_storage) / 2U;

    hbrick::HBrickIndexBuilder constrained_builder;
    constrained_builder.begin(graph, layout, tight_config);
    while (!constrained_builder.step()) {}

    EXPECT_NE(constrained_builder.report().status, hbrick::BaselineStatus::Completed);
    EXPECT_TRUE(constrained_builder.report().status == hbrick::BaselineStatus::OutOfMemory
                || constrained_builder.report().status == hbrick::BaselineStatus::SkippedByPolicy);
}

TEST(HBrickIndexBuilder, AsymmetricTileAndGroupDimensionsBuildCorrectly) {
    // 16x12 map with 4x3 base tiles (4x4 slot grid = 16 base tiles) and 2x2 grouping
    hbrick::MazeLayout layout(16U, 12U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{4U, 3U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    config.max_depth = 2U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickIndexBuilder builder;
    builder.begin(graph, layout, config);
    while (!builder.step()) {}

    const hbrick::HBrickBuildReport& report = builder.report();
    EXPECT_EQ(report.status, hbrick::BaselineStatus::Completed);
    EXPECT_EQ(report.num_base_tiles, 16U);
    EXPECT_EQ(report.super_levels.size(), 1U);
    EXPECT_EQ(report.super_levels.front().num_completed, 4U);

    const hbrick::HBrickIndex index = builder.takeIndex();
    EXPECT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    EXPECT_TRUE(index.hasSuperLevel(1U));
}

