#include <gtest/gtest.h>

#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "hbrick/baselines/csr_bfs_baseline.hpp"
#include "hbrick/baselines/csr_dfs_baseline.hpp"
#include "hbrick/baselines/full_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_search_baseline.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/passable_grid.hpp"

namespace {

struct BaselineCase {
    std::string name;
    hbrick::BaselineStatus status = hbrick::BaselineStatus::NotRun;
    uint32_t mismatches = 0;
};

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

hbrick::CsrGraph buildRandomGridGraph() {
    const hbrick::PassableGrid grid(5U, 4U);
    const hbrick::DirectedGridGraph directed = hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0x0123456789ABCDEFULL, 0.17L, 0.23L}
    );
    return directed.csrGraph();
}

hbrick::ReachabilityAnswer referenceReachable(
    const hbrick::CsrGraph& graph,
    const uint32_t source,
    const uint32_t target,
    hbrick::GraphSearchScratch& scratch
) {
    return hbrick::Bfs::reachable(graph, source, target, scratch);
}

uint32_t countMismatchesForBaseline(
    const hbrick::CsrGraph& graph,
    const std::function<hbrick::ReachabilityAnswer(uint32_t, uint32_t)>& baseline_query
) {
    hbrick::GraphSearchScratch scratch(graph.numVertices());
    uint32_t mismatches = 0;

    for (uint32_t source = 0; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer expected = referenceReachable(
                graph,
                source,
                target,
                scratch
            );
            const hbrick::ReachabilityAnswer actual = baseline_query(source, target);
            if (actual != expected) {
                ++mismatches;
            }
        }
    }

    return mismatches;
}

std::vector<BaselineCase> runCorrectnessHarness(const hbrick::CsrGraph& graph) {
    hbrick::GraphSearchScratch preprocess_scratch(graph.numVertices());
    hbrick::GraphSearchScratch query_scratch(graph.numVertices());
    const uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::CsrBfsBaseline csr_bfs;
    hbrick::CsrDfsBaseline csr_dfs;
    hbrick::SccDagSearchBaseline scc_search;
    hbrick::SccDagClosureBaseline scc_closure;
    hbrick::FullClosureBaseline full_closure;

    csr_bfs.preprocess(graph);
    csr_dfs.preprocess(graph);
    scc_search.preprocess(graph, preprocess_scratch);
    scc_closure.preprocess(graph, preprocess_scratch, max_memory_bytes);
    full_closure.preprocess(graph, max_memory_bytes);

    std::vector<BaselineCase> results{
        {"CsrBfs", csr_bfs.status(), 0U},
        {"CsrDfs", csr_dfs.status(), 0U},
        {"SccDagSearch", scc_search.status(), 0U},
        {"SccDagClosure", scc_closure.status(), 0U},
        {"FullClosure", full_closure.status(), 0U},
    };

    results[0U].mismatches = countMismatchesForBaseline(graph, [&](
        const uint32_t source,
        const uint32_t target
    ) {
        return csr_bfs.query(source, target, query_scratch);
    });
    results[1U].mismatches = countMismatchesForBaseline(graph, [&](
        const uint32_t source,
        const uint32_t target
    ) {
        return csr_dfs.query(source, target, query_scratch);
    });
    results[2U].mismatches = countMismatchesForBaseline(graph, [&](
        const uint32_t source,
        const uint32_t target
    ) {
        return scc_search.query(source, target, query_scratch);
    });
    results[3U].mismatches = countMismatchesForBaseline(graph, [&](
        const uint32_t source,
        const uint32_t target
    ) {
        return scc_closure.query(source, target);
    });
    results[4U].mismatches = countMismatchesForBaseline(graph, [&](
        const uint32_t source,
        const uint32_t target
    ) {
        return full_closure.query(source, target);
    });

    return results;
}

void expectHarnessPassesForGraph(const hbrick::CsrGraph& graph, const std::string& graph_name) {
    const std::vector<BaselineCase> results = runCorrectnessHarness(graph);

    for (const BaselineCase& result : results) {
        EXPECT_EQ(result.status, hbrick::BaselineStatus::Completed)
            << graph_name << " baseline=" << result.name;
        EXPECT_EQ(result.mismatches, 0U) << graph_name << " baseline=" << result.name;
    }
}

}  // namespace

TEST(CorrectnessHarness, AllBaselinesAgreeWithBfsOnDiamondGraph) {
    expectHarnessPassesForGraph(buildDiamondGraph(), "diamond");
}

TEST(CorrectnessHarness, AllBaselinesAgreeWithBfsOnCycleGraph) {
    expectHarnessPassesForGraph(buildCycleGraph(), "cycle");
}

TEST(CorrectnessHarness, AllBaselinesAgreeWithBfsOnRandomGridGraph) {
    expectHarnessPassesForGraph(buildRandomGridGraph(), "random-grid");
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
