#include <gtest/gtest.h>

#include "hbrick/baselines/csr_bfs_baseline.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
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

}  // namespace

TEST(CorrectnessHarness, AllBaselinesAgreeWithBfsOnDiamondGraph) {
    hbrick::test_support::expectAllBaselinesMatchBfs(buildDiamondGraph(), "diamond");
}

TEST(CorrectnessHarness, AllBaselinesAgreeWithBfsOnCycleGraph) {
    hbrick::test_support::expectAllBaselinesMatchBfs(buildCycleGraph(), "cycle");
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
