#include <gtest/gtest.h>

#include <limits>

#include "hbrick/baselines/brick_closure_baseline.hpp"
#include "hbrick/baselines/brick_search_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
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

void runBrickOracle(
    const hbrick::MazeLayout& layout,
    const hbrick::CsrGraph& graph,
    const hbrick::TileSize tile_size,
    const std::string& context
) {
    hbrick::test_support::expectBrickBaselinesMatchBfs(
        layout,
        graph,
        tile_size,
        context
    );
}

}  // namespace

TEST(BrickReachabilityIntegration, SearchAndClosureMatchBfsOnEightByEightRandomMaze) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{2U, 2U}, false);
    layout.setPassable(hbrick::GridCoord{5U, 5U}, false);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{11U, 0.6, 0.4, 0.0, 0.0}
    );

    runBrickOracle(layout, graph, hbrick::TileSize{4U, 4U}, "8x8-random-4x4-tiles");
}

TEST(BrickReachabilityIntegration, SearchAndClosureMatchBfsOnSixteenBySixteenBidirectionalMaze) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    runBrickOracle(layout, graph, hbrick::TileSize{4U, 4U}, "16x16-bidirectional-4x4-tiles");
}

TEST(BrickReachabilityIntegration, SearchAndClosureMatchBfsOnTwentyFourByTwentyFourMaze) {
    hbrick::MazeLayout layout(24U, 24U);
    layout.setPassable(hbrick::GridCoord{6U, 6U}, false);
    layout.setPassable(hbrick::GridCoord{12U, 12U}, false);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    runBrickOracle(layout, graph, hbrick::TileSize{4U, 4U}, "24x24-bidirectional-4x4-tiles");
}

TEST(BrickReachabilityIntegration, SearchAndClosureMatchBfsOnThirtyTwoByThirtyTwoRandomMaze) {
    hbrick::MazeLayout layout(32U, 32U);
    layout.setPassable(hbrick::GridCoord{8U, 8U}, false);
    layout.setPassable(hbrick::GridCoord{17U, 17U}, false);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{11U, 0.6, 0.4, 0.0, 0.0}
    );

    runBrickOracle(layout, graph, hbrick::TileSize{4U, 4U}, "32x32-random-4x4-tiles");
}

TEST(BrickReachabilityIntegration, SearchAndClosureMatchBfsWithEightByEightTiles) {
    hbrick::MazeLayout layout(32U, 32U);
    layout.setPassable(hbrick::GridCoord{10U, 10U}, false);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    runBrickOracle(layout, graph, hbrick::TileSize{8U, 8U}, "32x32-bidirectional-8x8-tiles");
}

TEST(BrickReachabilityIntegration, SameTileReentryWhenLocalClosureIsZero) {
    hbrick::MazeLayout layout(kReentryWidth, kReentryHeight, true);
    const hbrick::DirectedGridGraph graph = buildReentryGraph(layout);

    const uint32_t source = vertexAt(0U, 2U);
    const uint32_t target = vertexAt(3U, 2U);

    hbrick::BrickSearchBaseline search;
    search.preprocess(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(search.status(), hbrick::BaselineStatus::Completed);

    hbrick::BrickClosureBaseline closure;
    closure.preprocess(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(closure.status(), hbrick::BaselineStatus::Completed);

    const hbrick::BaseTileSummary& summary = search.index().tiles().summary(0U, 0U);
    const uint32_t source_local = search.index().tiles().localIndexForGlobalVertex(source);
    const uint32_t target_local = search.index().tiles().localIndexForGlobalVertex(target);
    ASSERT_NE(source_local, std::numeric_limits<uint32_t>::max());
    ASSERT_NE(target_local, std::numeric_limits<uint32_t>::max());
    EXPECT_FALSE(summary.local_closure.test(source_local, target_local));

    hbrick::GraphSearchScratch scratch(graph.numVertices());
    const hbrick::ReachabilityAnswer expected = hbrick::Bfs::reachable(
        graph.csrGraph(),
        source,
        target,
        scratch
    );
    EXPECT_EQ(expected, hbrick::ReachabilityAnswer::Reachable);
    EXPECT_EQ(search.query(source, target), expected);
    EXPECT_EQ(closure.query(source, target), expected);
}

TEST(BrickReachabilityIntegration, SearchAndClosureMatchBfsOnDisconnectedRegions) {
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

    runBrickOracle(layout, graph, hbrick::TileSize{4U, 4U}, "16x8-disconnected-left-half");
}

TEST(BrickReachabilityIntegration, SearchAndClosureAgreeOnPerfectMazeOracleHarness) {
    const hbrick::MazeLayout maze = hbrick::test_support::generatePerfectMaze(
        hbrick::test_support::MazeParams{12U, 10U, 0xB21C01ULL}
    );
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        maze,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0x515EED01ULL, 0.14L, 0.26L}
    );

    hbrick::test_support::expectReachabilityOracleAllSlices(
        graph,
        "perfect-12x10-brick-oracle vertices=" + std::to_string(graph.numVertices()),
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::test_support::kFullAllPairsVertexLimit,
        std::numeric_limits<uint64_t>::max(),
        &maze
    );
}

TEST(BrickReachabilityIntegration, SearchAndClosureAnswersMatchEachOtherOnRandomMaze) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{3U, 3U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0xBADC0DEULL, 0.5, 0.5, 0.0, 0.0}
    );

    hbrick::BrickSearchBaseline search;
    search.preprocess(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    hbrick::BrickClosureBaseline closure;
    closure.preprocess(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(search.status(), hbrick::BaselineStatus::Completed);
    ASSERT_EQ(closure.status(), hbrick::BaselineStatus::Completed);

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            EXPECT_EQ(search.query(source, target), closure.query(source, target))
                << "source=" << source << " target=" << target;
        }
    }
}
