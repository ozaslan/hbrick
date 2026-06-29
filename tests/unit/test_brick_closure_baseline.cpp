#include <gtest/gtest.h>

#include <limits>

#include "hbrick/baselines/brick_closure_baseline.hpp"
#include "hbrick/baselines/brick_search_baseline.hpp"
#include "hbrick/baselines/closure_matrix_builder.hpp"
#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/connected_components.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/kleene_squaring_bounds.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/tile_size.hpp"
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

hbrick::DirectedGridGraph buildRandomGraph(hbrick::MazeLayout& layout) {
    return hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{11U, 0.6, 0.4, 0.0, 0.0}
    );
}

void expectPortKleeneClosureMatchesWarshallOracle(
    const hbrick::BrickIndex& index,
    const uint64_t max_memory_bytes
) {
    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);

    hbrick::BitMatrix kleene = hbrick::ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(
        index.portGraph(),
        max_memory_bytes
    );
    hbrick::BitMatrix kleene_scratch{};
    hbrick::ClosureMatrixBuilder::transitiveClosureKleeneTruncatedInPlace(
        kleene,
        index.portGraph(),
        &kleene_scratch
    );

    hbrick::BitMatrix warshall = hbrick::ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(
        index.portGraph(),
        max_memory_bytes
    );
    hbrick::ClosureMatrixBuilder::transitiveClosureWarshallOracleInPlace(warshall);

    EXPECT_TRUE(hbrick::bitMatricesEqual(kleene, warshall));

    hbrick::GraphSearchScratch scc_scratch{index.portGraph().numVertices()};
    const uint32_t k_trunc =
        hbrick::kleeneSquaringCountForCsrGraph(index.portGraph(), scc_scratch);
    const uint32_t k_full =
        hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(
            index.portGraph().numVertices()
        );
    if (k_trunc != k_full) {
        hbrick::BitMatrix kleene_full =
            hbrick::ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(
                index.portGraph(),
                max_memory_bytes
            );
        hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            kleene_full,
            k_full,
            &kleene_scratch
        );
        EXPECT_TRUE(hbrick::bitMatricesEqual(kleene_full, warshall));
    }
}

void expectPortClosureMatchesPortBfs(
    const hbrick::BrickIndex& index,
    const hbrick::BitMatrix& port_closure
) {
    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    const hbrick::CsrGraph& port_graph = index.portGraph();
    ASSERT_EQ(port_closure.numRows(), port_graph.numVertices());
    ASSERT_EQ(port_closure.numCols(), port_graph.numVertices());

    hbrick::GraphSearchScratch scratch(port_graph.numVertices());
    for (uint32_t source = 0U; source < port_graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < port_graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer bfs_answer =
                hbrick::Bfs::reachable(port_graph, source, target, scratch);
            const bool closure_reachable = port_closure.test(source, target);
            EXPECT_EQ(
                closure_reachable,
                bfs_answer == hbrick::ReachabilityAnswer::Reachable
            ) << "port source=" << source << " target=" << target;
        }
    }
}

void expectBaselinePortMatrixMatchesIndependentKleene(
    const hbrick::MazeLayout& layout,
    const hbrick::DirectedGridGraph& graph,
    const hbrick::TileSize tile_size
) {
    hbrick::BrickClosureBaseline baseline;
    baseline.preprocess(
        graph,
        layout,
        tile_size,
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    hbrick::BitMatrix independent =
        hbrick::ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(
            baseline.index().portGraph(),
            std::numeric_limits<uint64_t>::max()
        );
    hbrick::BitMatrix scratch{};
    hbrick::ClosureMatrixBuilder::transitiveClosureKleeneTruncatedInPlace(
        independent,
        baseline.index().portGraph(),
        &scratch
    );

    EXPECT_TRUE(hbrick::bitMatricesEqual(baseline.portClosure(), independent));
    expectPortClosureMatchesPortBfs(baseline.index(), baseline.portClosure());
}

void expectMatchesBfsOnAllPairs(
    const hbrick::MazeLayout& layout,
    const hbrick::DirectedGridGraph& graph,
    const hbrick::TileSize tile_size
) {
    hbrick::test_support::expectBrickBaselinesMatchBfs(
        layout,
        graph.csrGraph(),
        tile_size,
        "brick-closure-unit"
    );
}

}  // namespace

TEST(BrickClosureBaseline, PortClosureMatrixMatchesWarshallOracleOnRandomMaze) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{2U, 2U}, false);
    layout.setPassable(hbrick::GridCoord{5U, 5U}, false);
    const hbrick::DirectedGridGraph graph = buildRandomGraph(layout);

    const hbrick::BrickIndex index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    expectPortKleeneClosureMatchesWarshallOracle(index, std::numeric_limits<uint64_t>::max());
}

TEST(BrickClosureBaseline, PortClosureMatrixMatchesWarshallOracleOnBidirectionalMaze) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const hbrick::BrickIndex index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    expectPortKleeneClosureMatchesWarshallOracle(index, std::numeric_limits<uint64_t>::max());
}

TEST(BrickClosureBaseline, MatchesBfsOnEightByEightRandomMaze) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{2U, 2U}, false);
    layout.setPassable(hbrick::GridCoord{5U, 5U}, false);
    const hbrick::DirectedGridGraph graph = buildRandomGraph(layout);

    expectMatchesBfsOnAllPairs(layout, graph, hbrick::TileSize{4U, 4U});
}

TEST(BrickClosureBaseline, MatchesBfsOnSixteenBySixteenBidirectionalMaze) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    expectMatchesBfsOnAllPairs(layout, graph, hbrick::TileSize{4U, 4U});
    expectBaselinePortMatrixMatchesIndependentKleene(
        layout,
        graph,
        hbrick::TileSize{4U, 4U}
    );
}

TEST(BrickClosureBaseline, MatchesBfsOnTwentyFourByTwentyFourBidirectionalMaze) {
    hbrick::MazeLayout layout(24U, 24U);
    layout.setPassable(hbrick::GridCoord{6U, 6U}, false);
    layout.setPassable(hbrick::GridCoord{12U, 12U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    expectMatchesBfsOnAllPairs(layout, graph, hbrick::TileSize{4U, 4U});
}

TEST(BrickClosureBaseline, MatchesBfsOnThirtyTwoByThirtyTwoRandomMaze) {
    hbrick::MazeLayout layout(32U, 32U);
    layout.setPassable(hbrick::GridCoord{8U, 8U}, false);
    layout.setPassable(hbrick::GridCoord{17U, 17U}, false);
    const hbrick::DirectedGridGraph graph = buildRandomGraph(layout);

    expectMatchesBfsOnAllPairs(layout, graph, hbrick::TileSize{4U, 4U});
    expectBaselinePortMatrixMatchesIndependentKleene(
        layout,
        graph,
        hbrick::TileSize{4U, 4U}
    );
}

TEST(BrickClosureBaseline, MatchesBfsWithEightByEightTiles) {
    hbrick::MazeLayout layout(32U, 32U);
    layout.setPassable(hbrick::GridCoord{10U, 10U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    expectMatchesBfsOnAllPairs(layout, graph, hbrick::TileSize{8U, 8U});
}

TEST(BrickClosureBaseline, SameTileReentryWhenLocalClosureIsZero) {
    hbrick::MazeLayout layout(kReentryWidth, kReentryHeight, true);
    const hbrick::DirectedGridGraph graph = buildReentryGraph(layout);

    const uint32_t source = vertexAt(0U, 2U);
    const uint32_t target = vertexAt(3U, 2U);

    hbrick::BrickClosureBaseline baseline;
    baseline.preprocess(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    const hbrick::BaseTileSummary& summary = baseline.index().tiles().summary(0U, 0U);
    const uint32_t source_local = baseline.index().tiles().localIndexForGlobalVertex(source);
    const uint32_t target_local = baseline.index().tiles().localIndexForGlobalVertex(target);
    ASSERT_NE(source_local, std::numeric_limits<uint32_t>::max());
    ASSERT_NE(target_local, std::numeric_limits<uint32_t>::max());
    EXPECT_FALSE(summary.local_closure.test(source_local, target_local));

    hbrick::GraphSearchScratch scratch(graph.numVertices());
    EXPECT_EQ(
        hbrick::Bfs::reachable(graph.csrGraph(), source, target, scratch),
        hbrick::ReachabilityAnswer::Reachable
    );
    EXPECT_EQ(
        baseline.query(source, target),
        hbrick::ReachabilityAnswer::Reachable
    );
}

TEST(BrickClosureBaseline, MatchesBfsOnDisconnectedRegions) {
    hbrick::MazeLayout layout(16U, 8U);
    for (uint32_t y = 0U; y < 8U; ++y) {
        for (uint32_t x = 0U; x < 8U; ++x) {
            layout.setPassable(x, y, false);
        }
    }
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    expectMatchesBfsOnAllPairs(layout, graph, hbrick::TileSize{4U, 4U});

    const hbrick::BrickIndex index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    expectPortKleeneClosureMatchesWarshallOracle(index, std::numeric_limits<uint64_t>::max());
}

TEST(BrickClosureBaseline, SkippedWhenPortClosureMemoryBudgetExceeded) {
    hbrick::MazeLayout layout(8U, 8U);
    const hbrick::DirectedGridGraph graph = buildRandomGraph(layout);

    hbrick::BrickClosureBaseline baseline;
    baseline.preprocess(graph, layout, hbrick::TileSize{4U, 4U}, 0U);
    EXPECT_EQ(baseline.status(), hbrick::BaselineStatus::SkippedByPolicy);
}

