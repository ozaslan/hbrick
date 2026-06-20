#include <gtest/gtest.h>

#include <limits>

#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
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
    hbrick::HBrickBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    const hbrick::CsrGraph& csr = graph.csrGraph();

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer expected =
                hbrick::Bfs::reachable(csr, source, target, bfs_scratch);
            const hbrick::ReachabilityAnswer actual = baseline.query(source, target);
            EXPECT_EQ(actual, expected) << "source=" << source << " target=" << target;
        }
    }
}

}  // namespace

TEST(HBrickBaseline, MatchesBfsOnEightByEightHierarchy) {
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

TEST(HBrickBaseline, MatchesBfsOnSixteenBySixteenFullDepth) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
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

TEST(HBrickBaseline, FlatFallbackWhenMaxDepthIsOne) {
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

TEST(HBrickBaseline, SkippedWhenMemoryBudgetExceeded) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::HBrickConfig config =
        configFor(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 2U);
    config.max_memory_bytes = 0U;

    hbrick::HBrickBaseline baseline;
    baseline.preprocess(graph, layout, config);
    EXPECT_EQ(baseline.status(), hbrick::BaselineStatus::SkippedByPolicy);
}
