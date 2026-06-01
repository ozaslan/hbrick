#include <gtest/gtest.h>

#include <limits>

#include "hbrick/baselines/closure_matrix_builder.hpp"
#include "hbrick/baselines/scc_dag_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_search_baseline.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/condensation_graph.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/graph/scc_decomposition.hpp"
#include "hbrick/grid/passable_grid.hpp"

namespace {

hbrick::CsrGraph buildCycleGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 2U);
    builder.addEdge(2U, 0U);
    builder.addEdge(2U, 3U);
    return builder.build();
}

hbrick::ReachabilityAnswer bfsAnswer(
    const hbrick::CsrGraph& graph,
    const uint32_t source,
    const uint32_t target,
    hbrick::GraphSearchScratch& scratch
) {
    return hbrick::Bfs::reachable(graph, source, target, scratch);
}

void expectBaselinesMatchBfsOnAllPairs(const hbrick::CsrGraph& graph) {
    hbrick::GraphSearchScratch preprocess_scratch(graph.numVertices());
    hbrick::GraphSearchScratch query_scratch(graph.numVertices());

    hbrick::SccDagSearchBaseline search_baseline;
    search_baseline.preprocess(graph, preprocess_scratch);

    hbrick::SccDagClosureBaseline closure_baseline;
    closure_baseline.preprocess(
        graph,
        preprocess_scratch,
        std::numeric_limits<uint64_t>::max()
    );

    ASSERT_EQ(search_baseline.status(), hbrick::BaselineStatus::Completed);
    ASSERT_EQ(closure_baseline.status(), hbrick::BaselineStatus::Completed);

    for (uint32_t source = 0; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer expected = bfsAnswer(
                graph,
                source,
                target,
                query_scratch
            );
            const hbrick::ReachabilityAnswer search_answer = search_baseline.query(
                source,
                target,
                query_scratch
            );
            const hbrick::ReachabilityAnswer closure_answer = closure_baseline.query(
                source,
                target
            );

            EXPECT_EQ(search_answer, expected) << "search source=" << source << " target=" << target;
            EXPECT_EQ(closure_answer, expected) << "closure source=" << source << " target=" << target;
        }
    }
}

}  // namespace

TEST(SccDecomposition, FindsStronglyConnectedComponents) {
    const hbrick::CsrGraph graph = buildCycleGraph();
    hbrick::GraphSearchScratch scratch(graph.numVertices());

    const hbrick::SccDecomposition decomposition = hbrick::SccDecomposition::compute(graph, scratch);
    const hbrick::CondensationGraph condensation =
        hbrick::CondensationGraph::fromGraph(graph, decomposition);

    EXPECT_EQ(decomposition.numComponents(), 2U);
    EXPECT_EQ(decomposition.componentOf(0U), decomposition.componentOf(1U));
    EXPECT_EQ(decomposition.componentOf(1U), decomposition.componentOf(2U));
    EXPECT_NE(decomposition.componentOf(2U), decomposition.componentOf(3U));
    EXPECT_EQ(condensation.dag().numEdges(), 1U);
}

TEST(SccDagBaselines, SearchAndClosureAgreeWithBfsOnKnownGraph) {
    expectBaselinesMatchBfsOnAllPairs(buildCycleGraph());
}

TEST(SccDagBaselines, SearchAndClosureAgreeWithBfsOnRandomSmallGraph) {
    const hbrick::PassableGrid grid(4U, 3U);
    const hbrick::DirectedGridGraph directed = hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0xCAFEBABEULL, 0.15L, 0.20L}
    );

    expectBaselinesMatchBfsOnAllPairs(directed.csrGraph());
}

TEST(SccDagClosureBaseline, SkippedByPolicyWhenMemoryEstimateExceeded) {
    hbrick::CsrGraphBuilder builder{65U};
    const hbrick::CsrGraph graph = builder.build();

    const uint64_t estimate = hbrick::ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes(
        graph.numVertices()
    );

    hbrick::GraphSearchScratch scratch(graph.numVertices());
    hbrick::SccDagClosureBaseline baseline;
    baseline.preprocess(graph, scratch, estimate - 1U);

    EXPECT_EQ(baseline.status(), hbrick::BaselineStatus::SkippedByPolicy);
    EXPECT_EQ(
        baseline.query(0U, 1U),
        hbrick::ReachabilityAnswer::Unreachable
    );
}

TEST(SccDagClosureBaseline, ChecksMemoryBeforeAllocation) {
    hbrick::CsrGraphBuilder builder{65U};
    const hbrick::CsrGraph graph = builder.build();
    const uint64_t limit = 512U;

    EXPECT_FALSE(hbrick::ClosureMatrixBuilder::canAllocateReflexiveAdjacency(
        graph.numVertices(),
        limit
    ));

    hbrick::GraphSearchScratch scratch(graph.numVertices());
    hbrick::SccDagClosureBaseline baseline;
    baseline.preprocess(graph, scratch, limit);

    EXPECT_EQ(baseline.status(), hbrick::BaselineStatus::SkippedByPolicy);
}
