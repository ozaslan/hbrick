#include <gtest/gtest.h>

#include <limits>

#include "hbrick/baselines/brick_closure_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/tile_size.hpp"

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

void expectMatchesBfsOnAllPairs(
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

    hbrick::GraphSearchScratch scratch(graph.numVertices());
    const hbrick::CsrGraph& csr = graph.csrGraph();

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer expected =
                hbrick::Bfs::reachable(csr, source, target, scratch);
            const hbrick::ReachabilityAnswer actual =
                baseline.query(source, target);
            EXPECT_EQ(actual, expected) << "source=" << source << " target=" << target;
        }
    }
}

}  // namespace

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

TEST(BrickClosureBaseline, SkippedWhenPortClosureMemoryBudgetExceeded) {
    hbrick::MazeLayout layout(8U, 8U);
    const hbrick::DirectedGridGraph graph = buildRandomGraph(layout);

    hbrick::BrickClosureBaseline baseline;
    baseline.preprocess(graph, layout, hbrick::TileSize{4U, 4U}, 0U);
    EXPECT_EQ(baseline.status(), hbrick::BaselineStatus::SkippedByPolicy);
}

