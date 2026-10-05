#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "hbrick/baselines/csr_bfs_baseline.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "reachability_oracle.hpp"

namespace {

hbrick::CsrGraph buildDiamondGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 2U);
    builder.addEdge(1U, 3U);
    builder.addEdge(2U, 3U);
    return builder.build();
}

hbrick::CsrGraph buildCycleGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 2U);
    builder.addEdge(2U, 0U);
    builder.addEdge(2U, 3U);
    return builder.build();
}

hbrick::CsrGraph buildTwoSccBridgeGraph() {
    hbrick::CsrGraphBuilder builder{6U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 0U);
    builder.addEdge(1U, 2U);
    builder.addEdge(2U, 3U);
    builder.addEdge(3U, 4U);
    builder.addEdge(4U, 5U);
    builder.addEdge(5U, 3U);
    return builder.build();
}

hbrick::CsrGraph buildSelfLoopPlusIsolatesGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(1U, 1U);
    builder.addEdge(1U, 2U);
    return builder.build();
}

hbrick::CsrGraph buildForkJoinGraph() {
    hbrick::CsrGraphBuilder builder{7U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 2U);
    builder.addEdge(1U, 3U);
    builder.addEdge(2U, 3U);
    builder.addEdge(3U, 4U);
    builder.addEdge(3U, 5U);
    builder.addEdge(4U, 6U);
    builder.addEdge(5U, 6U);
    return builder.build();
}

}  // namespace

TEST(CorrectnessHarness, AllBaselinesAgreeWithBfsOnDiamondGraph) {
    hbrick::test_support::expectAllBaselinesMatchBfs(buildDiamondGraph(), "diamond");
}

TEST(CorrectnessHarness, AllBaselinesAgreeWithBfsOnCycleGraph) {
    hbrick::test_support::expectAllBaselinesMatchBfs(buildCycleGraph(), "cycle");
}

TEST(CorrectnessHarness, AllBaselinesAgreeWithBfsOnTwoSccBridgeGraph) {
    hbrick::test_support::expectAllBaselinesMatchBfs(
        buildTwoSccBridgeGraph(),
        "two-scc-bridge"
    );
}

TEST(CorrectnessHarness, AllBaselinesAgreeWithBfsOnSelfLoopPlusIsolates) {
    hbrick::test_support::expectAllBaselinesMatchBfs(
        buildSelfLoopPlusIsolatesGraph(),
        "self-loop-isolates"
    );
}

TEST(CorrectnessHarness, AllBaselinesAgreeWithBfsOnForkJoinGraph) {
    hbrick::test_support::expectAllBaselinesMatchBfs(buildForkJoinGraph(), "fork-join");
}

TEST(CorrectnessHarness, EmptyGraphCompletesWithoutPairs) {
    hbrick::CsrGraphBuilder builder{0U};
    hbrick::test_support::expectAllBaselinesMatchBfs(builder.build(), "empty");
}

TEST(CorrectnessHarness, SingleVertexIsReflexivelyReachable) {
    hbrick::CsrGraphBuilder builder{1U};
    const hbrick::CsrGraph graph = builder.build();
    hbrick::test_support::expectAllBaselinesMatchBfs(graph, "single-vertex");

    hbrick::GraphSearchScratch scratch(1U);
    EXPECT_EQ(
        hbrick::test_support::bfsReference(graph, 0U, 0U, scratch),
        hbrick::ReachabilityAnswer::Reachable
    );
}

TEST(CorrectnessHarness, HandCraftedFamilyMatrix) {
    const std::vector<std::pair<std::string, hbrick::CsrGraph>> graphs{
        {"diamond", buildDiamondGraph()},
        {"cycle", buildCycleGraph()},
        {"two-scc-bridge", buildTwoSccBridgeGraph()},
        {"self-loop-isolates", buildSelfLoopPlusIsolatesGraph()},
        {"fork-join", buildForkJoinGraph()},
    };

    for (const auto& [name, graph] : graphs) {
        hbrick::test_support::expectSccPartitionMatchesBidirectionalBfs(graph, name);
        hbrick::test_support::expectAllBaselinesMatchBfs(graph, name);
    }
}

TEST(CorrectnessHarness, SingleThreadedHarnessUsesIndependentScratch) {
    const hbrick::CsrGraph graph = buildDiamondGraph();
    hbrick::GraphSearchScratch scratch_a(graph.numVertices());
    hbrick::GraphSearchScratch scratch_b(graph.numVertices());

    hbrick::CsrBfsBaseline baseline;
    baseline.preprocess(graph);

    const hbrick::ReachabilityAnswer first = baseline.query(0U, 3U, scratch_a);
    const hbrick::ReachabilityAnswer second = baseline.query(0U, 3U, scratch_b);

    EXPECT_EQ(first, hbrick::ReachabilityAnswer::Reachable);
    EXPECT_EQ(second, first);
}
