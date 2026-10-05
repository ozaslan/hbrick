#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "hbrick/baselines/hbrick_micro_bfs_baseline.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/graph/tile_micro_bfs.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

hbrick::HBrickConfig configFor(
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

void expectMatchesBfsOnAllPairs(
    const hbrick::MazeLayout& layout,
    const hbrick::DirectedGridGraph& graph,
    const hbrick::HBrickConfig& config
) {
    hbrick::HBrickMicroBfsBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    const hbrick::CsrGraph& csr = graph.csrGraph();

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer expected =
                hbrick::Bfs::reachable(csr, source, target, bfs_scratch);
            const hbrick::ReachabilityAnswer actual = baseline.query(source, target);
            EXPECT_EQ(actual, expected)
                << "source=" << source << " (" << graph.coordFromVertex(source).x << ","
                << graph.coordFromVertex(source).y << ") target=" << target
                << " (" << graph.coordFromVertex(target).x << ","
                << graph.coordFromVertex(target).y << ")";
        }
    }
}

}  // namespace

TEST(TileMicroBfs, StrictlyConfinedInsideTileBoundingBox) {
    // 8x8 layout, tile bounds [0, 4) x [0, 4)
    hbrick::MazeLayout layout(8U, 8U, true);
    // Block direct internal route from (0,0) to (1,0)
    // but allow going out to (0, 4) which is outside the tile
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::GraphSearchScratch scratch(graph.numVertices());

    const uint32_t v_0_0 = graph.vertexFromCoord(hbrick::GridCoord{0U, 0U});
    const uint32_t v_1_1 = graph.vertexFromCoord(hbrick::GridCoord{1U, 1U});
    const uint32_t v_5_5 = graph.vertexFromCoord(hbrick::GridCoord{5U, 5U});

    // Both inside tile [0, 4) x [0, 4)
    EXPECT_EQ(
        hbrick::TileMicroBfs::reachable(
            graph,
            hbrick::GridCoord{0U, 0U},
            4U,
            4U,
            v_0_0,
            v_1_1,
            scratch
        ),
        hbrick::ReachabilityAnswer::Reachable
    );

    // Target outside tile bounds cannot be reached via micro-BFS
    EXPECT_EQ(
        hbrick::TileMicroBfs::reachable(
            graph,
            hbrick::GridCoord{0U, 0U},
            4U,
            4U,
            v_0_0,
            v_5_5,
            scratch
        ),
        hbrick::ReachabilityAnswer::Unreachable
    );
}

TEST(HBrickMicroBfsBaseline, MatchesBfsOnEightByEightHierarchy) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{2U, 2U}, false);
    layout.setPassable(hbrick::GridCoord{5U, 5U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{31U, 0.55, 0.45, 0.0, 0.0}
    );

    expectMatchesBfsOnAllPairs(
        layout,
        graph,
        configFor(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 2U)
    );
}

TEST(HBrickMicroBfsBaseline, MatchesBfsOnSixteenBySixteenFullDepth) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    layout.setPassable(hbrick::GridCoord{10U, 10U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    expectMatchesBfsOnAllPairs(
        layout,
        graph,
        configFor(
            hbrick::TileSize{4U, 4U},
            hbrick::GroupSize{2U, 2U},
            hbrick::kHBrickFullDepth
        )
    );
}

TEST(HBrickMicroBfsBaseline, FlatFallbackWhenMaxDepthIsOne) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    expectMatchesBfsOnAllPairs(
        layout,
        graph,
        configFor(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 1U)
    );
}

TEST(HBrickMicroBfsBaseline, SavesMemoryByOmittingLocalClosure) {
    hbrick::MazeLayout layout(16U, 16U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const hbrick::HBrickConfig config = configFor(
        hbrick::TileSize{8U, 8U},
        hbrick::GroupSize{2U, 2U},
        2U
    );

    hbrick::HBrickBaseline standard_hbrick;
    standard_hbrick.preprocess(graph, layout, config);
    ASSERT_EQ(standard_hbrick.status(), hbrick::BaselineStatus::Completed);

    hbrick::HBrickMicroBfsBaseline micro_bfs_hbrick;
    micro_bfs_hbrick.preprocess(graph, layout, config);
    ASSERT_EQ(micro_bfs_hbrick.status(), hbrick::BaselineStatus::Completed);

    // Verify local_closure is empty in micro-BFS base tiles
    const auto& base_summaries = micro_bfs_hbrick.index().brickIndex().tiles().summaries();
    ASSERT_FALSE(base_summaries.empty());
    for (const auto& summary : base_summaries) {
        EXPECT_TRUE(summary.omit_local_closure);
        EXPECT_EQ(summary.local_closure.numRows(), 0U);
        EXPECT_EQ(summary.local_closure.numCols(), 0U);
    }

    // Verify storage reduction
    EXPECT_LT(micro_bfs_hbrick.measuredStorageBytes(), standard_hbrick.measuredStorageBytes());
    EXPECT_LT(micro_bfs_hbrick.indexStorageBytes(), standard_hbrick.indexStorageBytes());
}

TEST(HBrickMicroBfsBaseline, ZeroAllocationsOnQueryPath) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::HBrickConfig config = configFor(
        hbrick::TileSize{4U, 4U},
        hbrick::GroupSize{2U, 2U},
        2U
    );

    hbrick::HBrickMicroBfsBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    const std::size_t micro_bfs_q_cap = baseline.microBfsScratch().queue().capacity();
    const std::size_t micro_bfs_v_cap = baseline.microBfsScratch().visitedMark().capacity();
    const std::size_t port_bfs_q_cap = baseline.portBfsScratch().queue().capacity();
    const std::size_t port_bfs_v_cap = baseline.portBfsScratch().visitedMark().capacity();

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(baseline.query(source, target));
        }
    }

    EXPECT_EQ(baseline.microBfsScratch().queue().capacity(), micro_bfs_q_cap);
    EXPECT_EQ(baseline.microBfsScratch().visitedMark().capacity(), micro_bfs_v_cap);
    EXPECT_EQ(baseline.portBfsScratch().queue().capacity(), port_bfs_q_cap);
    EXPECT_EQ(baseline.portBfsScratch().visitedMark().capacity(), port_bfs_v_cap);
}
