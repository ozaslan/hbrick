#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "hbrick/graph/directed_grid_graph_builder.hpp"
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
