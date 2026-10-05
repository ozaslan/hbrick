#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "hbrick/baselines/oreach_baseline.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "maze_generator.hpp"

namespace {

hbrick::ReachabilityAnswer referenceBfs(
    const hbrick::CsrGraph& graph,
    const uint32_t source,
    const uint32_t target,
    hbrick::GraphSearchScratch& scratch
) {
    return hbrick::Bfs::reachable(graph, source, target, scratch);
}

void expectOreachMatchesBfsOnAllPairs(
    const hbrick::CsrGraph& graph,
    const hbrick::OreachBaselineParams& params = {}
) {
    hbrick::GraphSearchScratch scratch(graph.numVertices());
    hbrick::OreachBaseline baseline;
    baseline.preprocess(graph, params, std::numeric_limits<uint64_t>::max());

    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer expected = referenceBfs(
                graph,
                source,
                target,
                scratch
            );
            const hbrick::ReachabilityAnswer actual = baseline.query(
                source,
                target,
                scratch
            );
            EXPECT_EQ(actual, expected)
                << "Mismatch at pair (" << source << ", " << target << ")";
        }
    }
}

}  // namespace

TEST(OreachBaselineTest, HandlesEmptyGraph) {
    const hbrick::CsrGraph empty_graph = hbrick::CsrGraphBuilder{0U}.build();
    hbrick::OreachBaseline baseline;
    baseline.preprocess(empty_graph, {}, std::numeric_limits<uint64_t>::max());

    EXPECT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);
    hbrick::GraphSearchScratch scratch(0U);
    EXPECT_EQ(baseline.query(0U, 0U, scratch), hbrick::ReachabilityAnswer::Unreachable);
}

TEST(OreachBaselineTest, HandlesSingleVertex) {
    const hbrick::CsrGraph graph = hbrick::CsrGraphBuilder{1U}.build();
    hbrick::OreachBaseline baseline;
    baseline.preprocess(graph, {}, std::numeric_limits<uint64_t>::max());

    EXPECT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);
    hbrick::GraphSearchScratch scratch(1U);
    EXPECT_EQ(baseline.query(0U, 0U, scratch), hbrick::ReachabilityAnswer::Reachable);
}

TEST(OreachBaselineTest, DiamondGraphMatchesBfs) {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 2U);
    builder.addEdge(1U, 3U);
    builder.addEdge(2U, 3U);
    const hbrick::CsrGraph graph = builder.build();

    expectOreachMatchesBfsOnAllPairs(graph);
}

TEST(OreachBaselineTest, CyclicComponentsMatchBfs) {
    // Component 1: 0 <-> 1
    // Component 2: 2 <-> 3
    // Bridge: 1 -> 2
    // Component 3: 4 (isolated)
    hbrick::CsrGraphBuilder builder{5U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 0U);
    builder.addEdge(2U, 3U);
    builder.addEdge(3U, 2U);
    builder.addEdge(1U, 2U);
    const hbrick::CsrGraph graph = builder.build();

    expectOreachMatchesBfsOnAllPairs(graph);
}

TEST(OreachBaselineTest, DirectedCycleMatchesBfs) {
    // Directed cycle 0 -> 1 -> 2 -> 0
    hbrick::CsrGraphBuilder builder{3U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 2U);
    builder.addEdge(2U, 0U);
    const hbrick::CsrGraph graph = builder.build();

    expectOreachMatchesBfsOnAllPairs(graph);
}

TEST(OreachBaselineTest, GridMazeMatchesBfs) {
    const hbrick::MazeLayout maze = hbrick::test_support::generatePerfectMaze(
        hbrick::test_support::MazeParams{6U, 6U, 0x12345678ULL}
    );
    hbrick::RandomAsymmetricParams orient_params;
    orient_params.seed = 0xABCDEF01ULL;
    orient_params.p_bidirectional = 0.5L;
    orient_params.p_one_way = 0.5L;

    const hbrick::DirectedGridGraph grid_graph =
        hbrick::DirectedGridGraphBuilder::build(
            maze,
            hbrick::GridEdgeConversionMode::RandomAsymmetric,
            orient_params
        );
    const hbrick::CsrGraph graph = grid_graph.csrGraph();

    expectOreachMatchesBfsOnAllPairs(graph);
}

TEST(OreachBaselineTest, EnforcesMemoryLimitPolicy) {
    hbrick::CsrGraphBuilder builder{10U};
    for (uint32_t i = 0U; i < 9U; ++i) {
        builder.addEdge(i, i + 1U);
    }
    const hbrick::CsrGraph graph = builder.build();

    hbrick::OreachBaseline baseline;
    baseline.preprocess(graph, {}, 10ULL);  // tiny byte budget

    EXPECT_EQ(baseline.status(), hbrick::BaselineStatus::SkippedByPolicy);
    EXPECT_EQ(baseline.indexStorageBytes(), 0ULL);
}

TEST(OreachBaselineTest, QueryDetailedReportsObservationHit) {
    hbrick::CsrGraphBuilder builder{3U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 2U);
    const hbrick::CsrGraph graph = builder.build();

    hbrick::OreachBaseline baseline;
    baseline.preprocess(graph, {}, std::numeric_limits<uint64_t>::max());
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    hbrick::GraphSearchScratch scratch(3U);
    const hbrick::OreachQueryOutcome self_query = baseline.queryDetailed(1U, 1U, scratch);
    EXPECT_EQ(self_query.answer, hbrick::ReachabilityAnswer::Reachable);
    EXPECT_TRUE(self_query.settled_by_observation);
}
