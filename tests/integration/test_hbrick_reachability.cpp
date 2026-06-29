#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"
#include "maze_generator.hpp"
#include "reachability_oracle.hpp"

namespace {

constexpr uint32_t kReentryWidth = 8U;
constexpr uint32_t kReentryHeight = 4U;

uint32_t vertexAt(const uint32_t x, const uint32_t y) {
    return y * kReentryWidth + x;
}

void addEdge(
    hbrick::CsrGraphBuilder& builder,
    const hbrick::GridCoord from,
    const hbrick::GridCoord to
) {
    builder.addEdge(vertexAt(from.x, from.y), vertexAt(to.x, to.y));
}

hbrick::DirectedGridGraph buildReentryGraph(hbrick::MazeLayout& layout) {
    layout = hbrick::MazeLayout{kReentryWidth, kReentryHeight, true};
    hbrick::CsrGraphBuilder builder{kReentryWidth * kReentryHeight};

    addEdge(builder, {0U, 2U}, {0U, 1U});
    addEdge(builder, {0U, 1U}, {1U, 1U});
    addEdge(builder, {1U, 1U}, {2U, 1U});
    addEdge(builder, {2U, 1U}, {3U, 1U});
    addEdge(builder, {3U, 1U}, {4U, 1U});

    addEdge(builder, {4U, 1U}, {5U, 1U});
    addEdge(builder, {5U, 1U}, {6U, 1U});
    addEdge(builder, {6U, 1U}, {7U, 1U});
    addEdge(builder, {7U, 1U}, {7U, 2U});
    addEdge(builder, {7U, 2U}, {6U, 2U});
    addEdge(builder, {6U, 2U}, {5U, 2U});
    addEdge(builder, {5U, 2U}, {4U, 2U});
    addEdge(builder, {4U, 2U}, {3U, 2U});

    return hbrick::DirectedGridGraph::fromCsr(
        kReentryWidth,
        kReentryHeight,
        builder.build()
    );
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

void runHBrickOracle(
    const hbrick::MazeLayout& layout,
    const hbrick::CsrGraph& graph,
    const hbrick::HBrickConfig& config,
    const std::string& context
) {
    hbrick::test_support::expectHBrickMatchesBfs(
        layout,
        graph,
        config,
        context
    );
}

}  // namespace

TEST(HBrickReachabilityIntegration, MatchesBfsOnEightByEightRandomMaze) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{2U, 2U}, false);
    layout.setPassable(hbrick::GridCoord{5U, 5U}, false);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{11U, 0.6, 0.4, 0.0, 0.0}
    );

    runHBrickOracle(
        layout,
        graph,
        openConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 2U),
        "8x8-random-4x4-tiles-2x2-group-depth2"
    );
}

TEST(HBrickReachabilityIntegration, MatchesBfsOnSixteenBySixteenFullDepth) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    runHBrickOracle(
        layout,
        graph,
        openConfig(
            hbrick::TileSize{4U, 4U},
            hbrick::GroupSize{2U, 2U},
            hbrick::kHBrickFullDepth
        ),
        "16x16-bidirectional-4x4-tiles-full-depth"
    );
}

TEST(HBrickReachabilityIntegration, MatchesBfsOnTwentyFourByTwentyFourMaze) {
    hbrick::MazeLayout layout(24U, 24U);
    layout.setPassable(hbrick::GridCoord{6U, 6U}, false);
    layout.setPassable(hbrick::GridCoord{12U, 12U}, false);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    runHBrickOracle(
        layout,
        graph,
        openConfig(
            hbrick::TileSize{4U, 4U},
            hbrick::GroupSize{2U, 2U},
            hbrick::kHBrickFullDepth
        ),
        "24x24-bidirectional-4x4-tiles-full-depth"
    );
}

TEST(HBrickReachabilityIntegration, MatchesBfsOnThirtyTwoByThirtyTwoRandomMaze) {
    hbrick::MazeLayout layout(32U, 32U);
    layout.setPassable(hbrick::GridCoord{8U, 8U}, false);
    layout.setPassable(hbrick::GridCoord{17U, 17U}, false);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{11U, 0.6, 0.4, 0.0, 0.0}
    );

    runHBrickOracle(
        layout,
        graph,
        openConfig(
            hbrick::TileSize{4U, 4U},
            hbrick::GroupSize{2U, 2U},
            hbrick::kHBrickFullDepth
        ),
        "32x32-random-4x4-tiles-full-depth"
    );
}

TEST(HBrickReachabilityIntegration, MatchesBfsWithEightByEightBaseTiles) {
    hbrick::MazeLayout layout(32U, 32U);
    layout.setPassable(hbrick::GridCoord{10U, 10U}, false);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    runHBrickOracle(
        layout,
        graph,
        openConfig(
            hbrick::TileSize{8U, 8U},
            hbrick::GroupSize{2U, 2U},
            hbrick::kHBrickFullDepth
        ),
        "32x32-bidirectional-8x8-tiles-full-depth"
    );
}

TEST(HBrickReachabilityIntegration, MatchesBfsOnNineBySevenPartialEdgeTiles) {
    hbrick::MazeLayout layout(9U, 7U);
    layout.setPassable(hbrick::GridCoord{3U, 3U}, false);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{17U, 0.55, 0.45, 0.0, 0.0}
    );

    runHBrickOracle(
        layout,
        graph,
        openConfig(
            hbrick::TileSize{4U, 4U},
            hbrick::GroupSize{2U, 2U},
            hbrick::kHBrickFullDepth
        ),
        "9x7-partial-edge-4x4-tiles-full-depth"
    );
}

TEST(HBrickReachabilityIntegration, MatchesBfsWithAsymmetricGrouping) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{5U, 5U}, false);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{29U, 0.5, 0.5, 0.0, 0.0}
    );

    runHBrickOracle(
        layout,
        graph,
        openConfig(
            hbrick::TileSize{4U, 4U},
            hbrick::GroupSize{4U, 1U},
            hbrick::kHBrickFullDepth
        ),
        "16x16-random-4x1-grouping-full-depth"
    );
}

TEST(HBrickReachabilityIntegration, FlatFallbackWhenMaxDepthIsOne) {
    hbrick::MazeLayout layout(16U, 16U, true);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    runHBrickOracle(
        layout,
        graph,
        openConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 1U),
        "16x16-bidirectional-max-depth-one"
    );
}

TEST(HBrickReachabilityIntegration, MatchesBfsOnDisconnectedRegions) {
    hbrick::MazeLayout layout(16U, 8U);
    for (uint32_t y = 0U; y < 8U; ++y) {
        for (uint32_t x = 0U; x < 8U; ++x) {
            layout.setPassable(x, y, false);
        }
    }
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    runHBrickOracle(
        layout,
        graph,
        openConfig(
            hbrick::TileSize{4U, 4U},
            hbrick::GroupSize{2U, 2U},
            hbrick::kHBrickFullDepth
        ),
        "16x8-disconnected-left-half"
    );
}

TEST(HBrickReachabilityIntegration, MatchesBfsOnPerfectMaze) {
    const hbrick::MazeLayout maze = hbrick::test_support::generatePerfectMaze(
        hbrick::test_support::MazeParams{12U, 10U, 0xB821C01ULL}
    );
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        maze,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0x515EED02ULL, 0.14L, 0.26L}
    );

    runHBrickOracle(
        maze,
        graph,
        openConfig(
            hbrick::TileSize{4U, 4U},
            hbrick::GroupSize{2U, 2U},
            hbrick::kHBrickFullDepth
        ),
        "perfect-12x10-hbrick-oracle"
    );
}

TEST(HBrickReachabilityIntegration, MatchesBfsOnCyclicMaze) {
    const hbrick::MazeLayout maze = hbrick::test_support::generateMazeWithExtraPassages(
        hbrick::test_support::MazeParams{10U, 10U, 0xC0C1CULL},
        0xFACEULL,
        12U
    );
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        maze,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0x515EED03ULL, 0.2L, 0.3L}
    );

    runHBrickOracle(
        maze,
        graph,
        openConfig(
            hbrick::TileSize{4U, 4U},
            hbrick::GroupSize{2U, 2U},
            2U
        ),
        "cyclic-10x10-hbrick-oracle-depth2"
    );
}

TEST(HBrickReachabilityIntegration, SameTileReentryWhenLocalClosureIsZero) {
    hbrick::MazeLayout layout(kReentryWidth, kReentryHeight, true);
    const hbrick::DirectedGridGraph graph = buildReentryGraph(layout);

    const uint32_t source = vertexAt(0U, 2U);
    const uint32_t target = vertexAt(3U, 2U);

    hbrick::HBrickBaseline baseline;
    baseline.preprocess(
        graph,
        layout,
        openConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 2U)
    );
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    hbrick::GraphSearchScratch scratch(graph.numVertices());
    const hbrick::ReachabilityAnswer expected = hbrick::Bfs::reachable(
        graph.csrGraph(),
        source,
        target,
        scratch
    );
    EXPECT_EQ(expected, hbrick::ReachabilityAnswer::Reachable);
    EXPECT_EQ(baseline.query(source, target), expected);
}
