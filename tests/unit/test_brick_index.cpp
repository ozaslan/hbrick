#include <gtest/gtest.h>

#include <limits>

#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/brick_index_builder.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

[[nodiscard]] uint64_t chargedBytesAfterBaseTiles(
    const hbrick::DirectedGridGraph& graph,
    const hbrick::MazeLayout& layout,
    const hbrick::TileSize tile_size
) {
    hbrick::BrickIndexBuilder builder;
    builder.begin(graph, layout, tile_size, std::numeric_limits<uint64_t>::max());
    while (builder.running()) {
        if (builder.progress().stage == hbrick::BrickIndexBuildStage::Finalize) {
            break;
        }
        const bool finished = builder.step();
        if (finished) {
            break;
        }
    }
    EXPECT_EQ(builder.progress().stage, hbrick::BrickIndexBuildStage::Finalize);
    return builder.chargedStorageBytes();
}

}  // namespace

TEST(BrickIndex, ChargedBytesMatchHeapAfterBaseTileSkip) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::TileSize tile_size{4U, 4U};

    hbrick::BrickIndexBuilder probe_builder;
    probe_builder.begin(graph, layout, tile_size, std::numeric_limits<uint64_t>::max());
    const bool finished_after_first_tile = probe_builder.step();
    EXPECT_FALSE(finished_after_first_tile);
    const uint64_t charged_after_first_tile = probe_builder.chargedStorageBytes();
    ASSERT_GT(charged_after_first_tile, 0U);

    const hbrick::BrickIndex skipped = hbrick::BrickIndex::build(
        graph,
        layout,
        tile_size,
        charged_after_first_tile
    );
    EXPECT_EQ(skipped.status(), hbrick::BaselineStatus::SkippedByPolicy);
    EXPECT_EQ(skipped.storageBytes(), skipped.measureStorageBytes());
    EXPECT_GT(skipped.storageBytes(), 0U);
    EXPECT_LE(skipped.storageBytes(), charged_after_first_tile);
    EXPECT_EQ(skipped.tiles().summaries().size(), 4U);
    EXPECT_EQ(
        skipped.tiles().summary(0U, 0U).status,
        hbrick::BaselineStatus::Completed
    );
    EXPECT_EQ(skipped.ports().numPorts(), 0U);
}

TEST(BrickIndex, ChargedBytesMatchHeapAfterFinalizeSkip) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::TileSize tile_size{4U, 4U};

    const uint64_t base_charged = chargedBytesAfterBaseTiles(graph, layout, tile_size);
    ASSERT_GT(base_charged, 0U);

    const hbrick::BrickIndex skipped = hbrick::BrickIndex::build(
        graph,
        layout,
        tile_size,
        base_charged
    );
    EXPECT_EQ(skipped.status(), hbrick::BaselineStatus::SkippedByPolicy);
    EXPECT_EQ(skipped.storageBytes(), skipped.measureStorageBytes());
    EXPECT_GT(skipped.storageBytes(), 0U);
    EXPECT_LE(skipped.storageBytes(), base_charged);
    EXPECT_EQ(skipped.ports().numPorts(), 0U);
    EXPECT_EQ(skipped.portGraph().numEdges(), 0U);
    EXPECT_TRUE(skipped.seamEdges().empty());
}

TEST(BrickIndex, ChargedBytesMatchHeapOnCompletedBuild) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::TileSize tile_size{4U, 4U};

    const hbrick::BrickIndex index = hbrick::BrickIndex::build(
        graph,
        layout,
        tile_size,
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    EXPECT_GT(index.storageBytes(), 0U);
    EXPECT_EQ(index.storageBytes(), index.measureStorageBytes());
    EXPECT_TRUE(index.seamEdges().empty());
}
