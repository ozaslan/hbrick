#include <gtest/gtest.h>

#include <limits>

#include "hbrick/baselines/closure_matrix_builder.hpp"
#include "hbrick/baselines/csr_bfs_baseline.hpp"
#include "hbrick/baselines/csr_dfs_baseline.hpp"
#include "hbrick/baselines/full_closure_baseline.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/passable_grid.hpp"

namespace {

hbrick::CsrGraph buildDiamondGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 2U);
    builder.addEdge(1U, 3U);
    builder.addEdge(2U, 3U);
    return builder.build();
}

hbrick::ReachabilityAnswer referenceBfs(
    const hbrick::CsrGraph& graph,
    const uint32_t source,
    const uint32_t target,
    hbrick::GraphSearchScratch& scratch
) {
    return hbrick::Bfs::reachable(graph, source, target, scratch);
}

void expectCsrBaselinesMatchReferenceOnAllPairs(const hbrick::CsrGraph& graph) {
    hbrick::GraphSearchScratch query_scratch(graph.numVertices());

    hbrick::CsrBfsBaseline bfs_baseline;
    hbrick::CsrDfsBaseline dfs_baseline;
    bfs_baseline.preprocess(graph);
    dfs_baseline.preprocess(graph);

    hbrick::FullClosureBaseline closure_baseline;
    closure_baseline.preprocess(graph, std::numeric_limits<uint64_t>::max());

    ASSERT_EQ(bfs_baseline.status(), hbrick::BaselineStatus::Completed);
    ASSERT_EQ(dfs_baseline.status(), hbrick::BaselineStatus::Completed);
    ASSERT_EQ(closure_baseline.status(), hbrick::BaselineStatus::Completed);

    for (uint32_t source = 0; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer expected = referenceBfs(
                graph,
                source,
                target,
                query_scratch
            );
            const hbrick::ReachabilityAnswer bfs_answer = bfs_baseline.query(
                source,
                target,
                query_scratch
            );
            const hbrick::ReachabilityAnswer dfs_answer = dfs_baseline.query(
                source,
                target,
                query_scratch
            );
            const hbrick::ReachabilityAnswer closure_answer = closure_baseline.query(
                source,
                target
            );

            EXPECT_EQ(bfs_answer, expected) << "bfs source=" << source << " target=" << target;
            EXPECT_EQ(dfs_answer, expected) << "dfs source=" << source << " target=" << target;
            EXPECT_EQ(closure_answer, expected) << "closure source=" << source << " target=" << target;
        }
    }
}

}  // namespace

TEST(CsrBaselines, BfsAndDfsAgreeWithDirectSearchOnKnownGraph) {
    expectCsrBaselinesMatchReferenceOnAllPairs(buildDiamondGraph());
}

TEST(CsrBaselines, BfsAndDfsAgreeWithDirectSearchOnRandomSmallGraph) {
    const hbrick::PassableGrid grid(4U, 3U);
    const hbrick::DirectedGridGraph directed = hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0xFEEDFACEULL, 0.18L, 0.22L}
    );

    expectCsrBaselinesMatchReferenceOnAllPairs(directed.csrGraph());
}

TEST(FullClosureBaseline, SkippedByPolicyWhenMemoryEstimateExceeded) {
    hbrick::CsrGraphBuilder builder{65U};
    const hbrick::CsrGraph graph = builder.build();
    const uint64_t estimate = hbrick::ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes(
        graph.numVertices()
    );

    hbrick::FullClosureBaseline baseline;
    baseline.preprocess(graph, estimate - 1U);

    EXPECT_EQ(baseline.status(), hbrick::BaselineStatus::SkippedByPolicy);
    EXPECT_EQ(baseline.query(0U, 1U), hbrick::ReachabilityAnswer::Unreachable);
}

TEST(FullClosureBaseline, ChecksMemoryBeforeAllocation) {
    hbrick::CsrGraphBuilder builder{65U};
    const hbrick::CsrGraph graph = builder.build();

    EXPECT_FALSE(hbrick::ClosureMatrixBuilder::canAllocateReflexiveAdjacency(
        graph.numVertices(),
        512U
    ));

    hbrick::FullClosureBaseline baseline;
    baseline.preprocess(graph, 512U);

    EXPECT_EQ(baseline.status(), hbrick::BaselineStatus::SkippedByPolicy);
}

TEST(FullClosureBaseline, CompletedClosureAgreesWithBfsOnDiamond) {
    const hbrick::CsrGraph graph = buildDiamondGraph();
    hbrick::GraphSearchScratch scratch(graph.numVertices());

    hbrick::FullClosureBaseline baseline;
    baseline.preprocess(graph, std::numeric_limits<uint64_t>::max());

    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);
    EXPECT_EQ(baseline.query(0U, 3U), hbrick::ReachabilityAnswer::Reachable);
    EXPECT_EQ(baseline.query(3U, 0U), hbrick::ReachabilityAnswer::Unreachable);
    EXPECT_EQ(
        baseline.query(0U, 3U),
        referenceBfs(graph, 0U, 3U, scratch)
    );
}

TEST(CsrBaselines, QueryReturnsUnreachableWhenNotPreprocessed) {
    hbrick::CsrBfsBaseline bfs_baseline;
    hbrick::CsrDfsBaseline dfs_baseline;
    hbrick::GraphSearchScratch scratch(1U);

    EXPECT_EQ(bfs_baseline.status(), hbrick::BaselineStatus::NotRun);
    EXPECT_EQ(
        bfs_baseline.query(0U, 0U, scratch),
        hbrick::ReachabilityAnswer::Unreachable
    );
    EXPECT_EQ(
        dfs_baseline.query(0U, 0U, scratch),
        hbrick::ReachabilityAnswer::Unreachable
    );
}
