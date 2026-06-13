#include "reachability_oracle.hpp"
#include "test_limits.hpp"

#include <gtest/gtest.h>

#include <iostream>
#include <vector>

#include "hbrick/baselines/csr_bfs_baseline.hpp"
#include "hbrick/baselines/csr_dfs_baseline.hpp"
#include "hbrick/baselines/full_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_search_baseline.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"

namespace hbrick::test_support {

namespace {

void logOracleProgress(const std::string& message) {
    std::cerr << "[reachability-oracle] " << message << std::endl;
}

void collectForwardReachable(
    const CsrGraph& graph,
    const uint32_t source,
    GraphSearchScratch& scratch,
    std::vector<uint32_t>& reachable_vertices
) {
    reachable_vertices.clear();
    if (source >= graph.numVertices()) {
        return;
    }

    const uint32_t mark = scratch.nextMark();
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<uint32_t>& queue = scratch.queue();
    queue.clear();

    visited[source] = mark;
    queue.push_back(source);

    std::size_t head = 0U;
    while (head < queue.size()) {
        const uint32_t vertex = queue[head];
        ++head;

        for (const uint32_t neighbor : graph.outNeighbors(vertex)) {
            if (visited[neighbor] == mark) {
                continue;
            }

            visited[neighbor] = mark;
            queue.push_back(neighbor);
        }
    }

    reachable_vertices.reserve(queue.size());
    for (const uint32_t vertex : queue) {
        reachable_vertices.push_back(vertex);
    }
}

[[nodiscard]] CsrGraph buildTransposeGraph(const CsrGraph& graph) {
    CsrGraphBuilder builder{graph.numVertices()};
    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (const uint32_t target : graph.outNeighbors(source)) {
            builder.addEdge(target, source);
        }
    }
    return builder.build();
}

void expectMutualClassMatchesComponent(
    const CsrGraph& graph,
    const CsrGraph& transpose,
    const SccDecomposition& decomposition,
    const uint32_t component_id,
    const uint32_t pivot,
    GraphSearchScratch& forward_scratch,
    GraphSearchScratch& reverse_scratch,
    const std::string& context
) {
    std::vector<uint32_t> forward_reachable;
    std::vector<uint32_t> backward_reachable;
    collectForwardReachable(graph, pivot, forward_scratch, forward_reachable);
    collectForwardReachable(transpose, pivot, reverse_scratch, backward_reachable);

    std::vector<uint8_t> forward_seen(graph.numVertices(), 0U);
    std::vector<uint8_t> backward_seen(graph.numVertices(), 0U);
    for (const uint32_t vertex : forward_reachable) {
        forward_seen[vertex] = 1U;
    }
    for (const uint32_t vertex : backward_reachable) {
        backward_seen[vertex] = 1U;
    }

    for (uint32_t vertex = 0U; vertex < graph.numVertices(); ++vertex) {
        const bool in_component = decomposition.componentOf(vertex) == component_id;
        const bool mutually_reachable_with_pivot =
            forward_seen[vertex] != 0U && backward_seen[vertex] != 0U;
        EXPECT_EQ(in_component, mutually_reachable_with_pivot)
            << context << " component=" << component_id << " pivot=" << pivot
            << " vertex=" << vertex;
    }
}

}  // namespace

ReachabilityAnswer bfsReference(
    const CsrGraph& graph,
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) {
    return Bfs::reachable(graph, source, target, scratch);
}

CsrGraph buildGridGraph(
    const MazeLayout& grid,
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

bool mutuallyReachableViaBidirectionalBfs(
    const CsrGraph& graph,
    const uint32_t left,
    const uint32_t right,
    GraphSearchScratch& left_to_right_scratch,
    GraphSearchScratch& right_to_left_scratch
) {
    if (left == right) {
        return true;
    }

    const ReachabilityAnswer left_to_right = bfsReference(
        graph,
        left,
        right,
        left_to_right_scratch
    );
    const ReachabilityAnswer right_to_left = bfsReference(
        graph,
        right,
        left,
        right_to_left_scratch
    );
    return left_to_right == ReachabilityAnswer::Reachable
        && right_to_left == ReachabilityAnswer::Reachable;
}

void expectSccPartitionMatchesBidirectionalBfs(
    const CsrGraph& graph,
    const std::string& context
) {
    GraphSearchScratch scc_scratch(graph.numVertices());
    GraphSearchScratch forward_scratch(graph.numVertices());
    GraphSearchScratch reverse_scratch(graph.numVertices());

    const SccDecomposition decomposition = SccDecomposition::compute(graph, scc_scratch);
    const CsrGraph transpose = buildTransposeGraph(graph);

    std::vector<std::vector<uint32_t>> components(decomposition.numComponents());
    for (uint32_t vertex = 0U; vertex < graph.numVertices(); ++vertex) {
        components[decomposition.componentOf(vertex)].push_back(vertex);
    }

    for (uint32_t component_id = 0U; component_id < decomposition.numComponents(); ++component_id) {
        const std::vector<uint32_t>& bucket = components[component_id];
        ASSERT_FALSE(bucket.empty()) << context << " component=" << component_id;

        if (bucket.size() == 1U) {
            EXPECT_EQ(decomposition.componentOf(bucket.front()), component_id)
                << context << " singleton vertex=" << bucket.front();
            continue;
        }

        expectMutualClassMatchesComponent(
            graph,
            transpose,
            decomposition,
            component_id,
            bucket.front(),
            forward_scratch,
            reverse_scratch,
            context
        );
    }
}

void expectExhaustiveReachabilityOracle(
    const CsrGraph& graph,
    const std::string& context,
    const GridEdgeConversionMode mode,
    const uint32_t full_all_pairs_vertex_limit,
    const uint64_t max_memory_bytes
) {
    expectReachabilityOracleAllSlices(
        graph,
        context,
        mode,
        full_all_pairs_vertex_limit,
        max_memory_bytes
    );
}

uint32_t computeReachabilityPairSliceCount(const uint32_t num_vertices) {
    if (num_vertices == 0U) {
        return 1U;
    }

    if (num_vertices <= kFullAllPairsVertexLimit) {
        return 1U;
    }

    const uint64_t total_pairs = static_cast<uint64_t>(num_vertices) * num_vertices;
    return static_cast<uint32_t>(
        (total_pairs + static_cast<uint64_t>(kMaxPairsPerSlice) - 1U)
            / static_cast<uint64_t>(kMaxPairsPerSlice)
    );
}

uint64_t reachabilityPairCountInSlice(
    const uint32_t num_vertices,
    const uint32_t slice_id,
    const uint32_t slice_count
) {
    if (num_vertices == 0U || slice_count == 0U || slice_id >= slice_count) {
        return 0U;
    }

    const uint64_t total_pairs = static_cast<uint64_t>(num_vertices) * num_vertices;
    if (slice_id >= total_pairs) {
        return 0U;
    }

    return (total_pairs - static_cast<uint64_t>(slice_id) + slice_count - 1U) / slice_count;
}

void expectSearchBaselinesMatchBfsOnSlice(
    const CsrGraph& graph,
    const std::string& context,
    const uint32_t slice_id,
    const uint32_t slice_count
) {
    if (graph.numVertices() == 0U || slice_count == 0U || slice_id >= slice_count) {
        return;
    }

    GraphSearchScratch preprocess_scratch(graph.numVertices());
    GraphSearchScratch query_scratch(graph.numVertices());

    CsrBfsBaseline csr_bfs;
    CsrDfsBaseline csr_dfs;
    SccDagSearchBaseline scc_search;

    csr_bfs.preprocess(graph);
    csr_dfs.preprocess(graph);
    scc_search.preprocess(graph, preprocess_scratch);

    ASSERT_EQ(csr_bfs.status(), BaselineStatus::Completed) << context;
    ASSERT_EQ(csr_dfs.status(), BaselineStatus::Completed) << context;
    ASSERT_EQ(scc_search.status(), BaselineStatus::Completed) << context;

    const uint32_t num_vertices = graph.numVertices();
    const uint64_t total_pairs = static_cast<uint64_t>(num_vertices) * num_vertices;
    const uint64_t pairs_in_slice =
        reachabilityPairCountInSlice(num_vertices, slice_id, slice_count);
    if (pairs_in_slice > 10000U) {
        logOracleProgress(
            context + " checking " + std::to_string(pairs_in_slice) + " search-baseline pairs"
        );
    }

    uint64_t pairs_checked = 0U;
    for (uint64_t pair_index = slice_id; pair_index < total_pairs; pair_index += slice_count) {
        const uint32_t source = static_cast<uint32_t>(pair_index / num_vertices);
        const uint32_t target = static_cast<uint32_t>(pair_index % num_vertices);
        const ReachabilityAnswer expected = bfsReference(
            graph,
            source,
            target,
            query_scratch
        );

        EXPECT_EQ(csr_bfs.query(source, target, query_scratch), expected)
            << context << " baseline=CsrBfs slice=" << slice_id << '/' << slice_count
            << " source=" << source << " target=" << target;
        EXPECT_EQ(csr_dfs.query(source, target, query_scratch), expected)
            << context << " baseline=CsrDfs slice=" << slice_id << '/' << slice_count
            << " source=" << source << " target=" << target;
        EXPECT_EQ(scc_search.query(source, target, query_scratch), expected)
            << context << " baseline=SccDagSearch slice=" << slice_id << '/'
            << slice_count << " source=" << source << " target=" << target;

        ++pairs_checked;
        if (pairs_in_slice > 10000U && pairs_checked % 10000U == 0U) {
            logOracleProgress(
                context + " " + std::to_string(pairs_checked) + '/'
                    + std::to_string(pairs_in_slice) + " pairs"
            );
        }
    }
}

void expectAllBaselinesMatchBfsOnSlice(
    const CsrGraph& graph,
    const std::string& context,
    const uint32_t slice_id,
    const uint32_t slice_count,
    const uint64_t max_memory_bytes
) {
    if (graph.numVertices() == 0U || slice_count == 0U || slice_id >= slice_count) {
        return;
    }

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

    ASSERT_EQ(csr_bfs.status(), BaselineStatus::Completed) << context;
    ASSERT_EQ(csr_dfs.status(), BaselineStatus::Completed) << context;
    ASSERT_EQ(scc_search.status(), BaselineStatus::Completed) << context;
    ASSERT_EQ(scc_closure.status(), BaselineStatus::Completed) << context;
    ASSERT_EQ(full_closure.status(), BaselineStatus::Completed) << context;

    const uint32_t num_vertices = graph.numVertices();
    const uint64_t total_pairs = static_cast<uint64_t>(num_vertices) * num_vertices;
    const uint64_t pairs_in_slice =
        reachabilityPairCountInSlice(num_vertices, slice_id, slice_count);
    if (pairs_in_slice > 10000U) {
        logOracleProgress(
            context + " checking " + std::to_string(pairs_in_slice) + " all-baseline pairs"
        );
    }

    uint64_t pairs_checked = 0U;
    for (uint64_t pair_index = slice_id; pair_index < total_pairs; pair_index += slice_count) {
        const uint32_t source = static_cast<uint32_t>(pair_index / num_vertices);
        const uint32_t target = static_cast<uint32_t>(pair_index % num_vertices);
        const ReachabilityAnswer expected = bfsReference(
            graph,
            source,
            target,
            query_scratch
        );

        EXPECT_EQ(csr_bfs.query(source, target, query_scratch), expected)
            << context << " baseline=CsrBfs slice=" << slice_id << '/' << slice_count
            << " source=" << source << " target=" << target;
        EXPECT_EQ(csr_dfs.query(source, target, query_scratch), expected)
            << context << " baseline=CsrDfs slice=" << slice_id << '/' << slice_count
            << " source=" << source << " target=" << target;
        EXPECT_EQ(scc_search.query(source, target, query_scratch), expected)
            << context << " baseline=SccDagSearch slice=" << slice_id << '/'
            << slice_count << " source=" << source << " target=" << target;
        EXPECT_EQ(scc_closure.query(source, target), expected)
            << context << " baseline=SccDagClosure slice=" << slice_id << '/'
            << slice_count << " source=" << source << " target=" << target;
        EXPECT_EQ(full_closure.query(source, target), expected)
            << context << " baseline=FullClosure slice=" << slice_id << '/'
            << slice_count << " source=" << source << " target=" << target;

        ++pairs_checked;
        if (pairs_in_slice > 10000U && pairs_checked % 10000U == 0U) {
            logOracleProgress(
                context + " " + std::to_string(pairs_checked) + '/'
                    + std::to_string(pairs_in_slice) + " pairs"
            );
        }
    }
}

void expectReachabilityOracleAllSlices(
    const CsrGraph& graph,
    const std::string& context,
    const GridEdgeConversionMode mode,
    const uint32_t full_all_pairs_vertex_limit,
    const uint64_t max_memory_bytes
) {
    (void)mode;

    const uint32_t slice_count = computeReachabilityPairSliceCount(graph.numVertices());
    if (slice_count > kMaxSlicesPerTestCase) {
        logOracleProgress(
            context + " pair oracle skipped (requires " + std::to_string(slice_count)
                + " slices, V=" + std::to_string(graph.numVertices()) + ')'
        );
        expectSccPartitionMatchesBidirectionalBfs(graph, context);
        logOracleProgress(context + " SCC partition verified (pair budget exceeded)");
        return;
    }

    logOracleProgress(
        context + " starting V=" + std::to_string(graph.numVertices()) + " slices="
            + std::to_string(slice_count)
    );

    expectSccPartitionMatchesBidirectionalBfs(graph, context);
    logOracleProgress(context + " SCC partition verified");

    if (graph.numVertices() <= full_all_pairs_vertex_limit) {
        ASSERT_EQ(slice_count, 1U) << context;
        expectAllBaselinesMatchBfsOnSlice(
            graph,
            context,
            0U,
            slice_count,
            max_memory_bytes
        );
        logOracleProgress(context + " finished");
        return;
    }

    for (uint32_t slice_id = 0U; slice_id < slice_count; ++slice_id) {
        const std::string slice_context = context + " slice=" + std::to_string(slice_id) + '/'
            + std::to_string(slice_count);
        logOracleProgress(slice_context + " begin");
        expectSearchBaselinesMatchBfsOnSlice(graph, slice_context, slice_id, slice_count);
        logOracleProgress(slice_context + " done");
    }

    logOracleProgress(context + " finished");
}

void expectReachabilityOracleSlice(
    const CsrGraph& graph,
    const std::string& context,
    const uint32_t slice_id,
    const GridEdgeConversionMode mode,
    const uint32_t full_all_pairs_vertex_limit,
    const uint64_t max_memory_bytes
) {
    (void)mode;

    const uint32_t slice_count = computeReachabilityPairSliceCount(graph.numVertices());

    if (slice_count > kMaxSlicesPerTestCase) {
        GTEST_SKIP() << context << " requires " << slice_count
                     << " pair slices (V²="
                     << static_cast<uint64_t>(graph.numVertices()) * graph.numVertices()
                     << "); raise kMaxPairsPerSlice or kMaxSlicesPerTestCase in test_limits.hpp";
    }

    if (slice_id >= slice_count) {
        return;
    }

    if (slice_id == 0U) {
        expectSccPartitionMatchesBidirectionalBfs(graph, context);
    }

    if (graph.numVertices() <= full_all_pairs_vertex_limit) {
        ASSERT_EQ(slice_count, 1U) << context;
        ASSERT_EQ(slice_id, 0U) << context;
        expectAllBaselinesMatchBfsOnSlice(
            graph,
            context,
            slice_id,
            slice_count,
            max_memory_bytes
        );
        return;
    }

    expectSearchBaselinesMatchBfsOnSlice(graph, context, slice_id, slice_count);
}

}  // namespace hbrick::test_support
