#include "reachability_oracle.hpp"

#include <gtest/gtest.h>

#include "hbrick/baselines/csr_bfs_baseline.hpp"
#include "hbrick/baselines/csr_dfs_baseline.hpp"
#include "hbrick/baselines/full_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_search_baseline.hpp"
#include "hbrick/graph/bfs.hpp"

namespace hbrick::test_support {

ReachabilityAnswer bfsReference(
    const CsrGraph& graph,
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) {
    return Bfs::reachable(graph, source, target, scratch);
}

CsrGraph buildGridGraph(
    const PassableGrid& grid,
    const GridEdgeConversionMode mode,
    RandomAsymmetricParams params
) {
    return DirectedGridGraphBuilder::build(grid, mode, params).csrGraph();
}

uint32_t countMismatchesAgainstBfs(
    const CsrGraph& graph,
    const std::function<ReachabilityAnswer(uint32_t, uint32_t)>& query
) {
    GraphSearchScratch scratch(graph.numVertices());
    uint32_t mismatches = 0U;

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const ReachabilityAnswer expected = bfsReference(graph, source, target, scratch);
            const ReachabilityAnswer actual = query(source, target);
            if (actual != expected) {
                ++mismatches;
            }
        }
    }

    return mismatches;
}

std::vector<BaselineOracleResult> runAllBaselinesAgainstBfs(
    const CsrGraph& graph,
    const uint64_t max_memory_bytes
) {
    GraphSearchScratch preprocess_scratch(graph.numVertices());
    GraphSearchScratch query_scratch(graph.numVertices());

    CsrBfsBaseline csr_bfs;
    CsrDfsBaseline csr_dfs;
    SccDagSearchBaseline scc_search;
    SccDagClosureBaseline scc_closure;
    FullClosureBaseline full_closure;

    csr_bfs.preprocess(graph);
    csr_dfs.preprocess(graph);
    scc_search.preprocess(graph, preprocess_scratch);
    scc_closure.preprocess(graph, preprocess_scratch, max_memory_bytes);
    full_closure.preprocess(graph, max_memory_bytes);

    std::vector<BaselineOracleResult> results{
        {"CsrBfs", csr_bfs.status(), 0U},
        {"CsrDfs", csr_dfs.status(), 0U},
        {"SccDagSearch", scc_search.status(), 0U},
        {"SccDagClosure", scc_closure.status(), 0U},
        {"FullClosure", full_closure.status(), 0U},
    };

    if (graph.numVertices() == 0U) {
        return results;
    }

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const ReachabilityAnswer expected = bfsReference(
                graph,
                source,
                target,
                query_scratch
            );

            if (results[0U].status == BaselineStatus::Completed) {
                const ReachabilityAnswer actual = csr_bfs.query(source, target, query_scratch);
                if (actual != expected) {
                    ++results[0U].mismatches;
                }
            }

            if (results[1U].status == BaselineStatus::Completed) {
                const ReachabilityAnswer actual = csr_dfs.query(source, target, query_scratch);
                if (actual != expected) {
                    ++results[1U].mismatches;
                }
            }

            if (results[2U].status == BaselineStatus::Completed) {
                const ReachabilityAnswer actual = scc_search.query(source, target, query_scratch);
                if (actual != expected) {
                    ++results[2U].mismatches;
                }
            }

            if (results[3U].status == BaselineStatus::Completed) {
                const ReachabilityAnswer actual = scc_closure.query(source, target);
                if (actual != expected) {
                    ++results[3U].mismatches;
                }
            }

            if (results[4U].status == BaselineStatus::Completed) {
                const ReachabilityAnswer actual = full_closure.query(source, target);
                if (actual != expected) {
                    ++results[4U].mismatches;
                }
            }
        }
    }

    return results;
}

void expectAllBaselinesMatchBfs(
    const CsrGraph& graph,
    const std::string& context,
    const uint64_t max_memory_bytes
) {
    const std::vector<BaselineOracleResult> results = runAllBaselinesAgainstBfs(
        graph,
        max_memory_bytes
    );

    for (const BaselineOracleResult& result : results) {
        EXPECT_EQ(result.status, BaselineStatus::Completed)
            << context << " baseline=" << result.name;
        EXPECT_EQ(result.mismatches, 0U) << context << " baseline=" << result.name;
    }
}

void expectSearchBaselinesMatchBfs(const CsrGraph& graph, const std::string& context) {
    GraphSearchScratch preprocess_scratch(graph.numVertices());
    GraphSearchScratch query_scratch(graph.numVertices());

    CsrBfsBaseline csr_bfs;
    CsrDfsBaseline csr_dfs;
    SccDagSearchBaseline scc_search;

    csr_bfs.preprocess(graph);
    csr_dfs.preprocess(graph);
    scc_search.preprocess(graph, preprocess_scratch);

    const std::vector<BaselineOracleResult> results{
        {"CsrBfs", csr_bfs.status(), 0U},
        {"CsrDfs", csr_dfs.status(), 0U},
        {"SccDagSearch", scc_search.status(), 0U},
    };

    for (const BaselineOracleResult& baseline : results) {
        EXPECT_EQ(baseline.status, BaselineStatus::Completed)
            << context << " baseline=" << baseline.name;
    }

    if (graph.numVertices() == 0U) {
        return;
    }

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const ReachabilityAnswer expected = bfsReference(
                graph,
                source,
                target,
                query_scratch
            );

            EXPECT_EQ(csr_bfs.query(source, target, query_scratch), expected)
                << context << " baseline=CsrBfs source=" << source << " target=" << target;
            EXPECT_EQ(csr_dfs.query(source, target, query_scratch), expected)
                << context << " baseline=CsrDfs source=" << source << " target=" << target;
            EXPECT_EQ(scc_search.query(source, target, query_scratch), expected)
                << context << " baseline=SccDagSearch source=" << source << " target=" << target;
        }
    }
}

}  // namespace hbrick::test_support
