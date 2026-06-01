#include <gtest/gtest.h>

#include <limits>

#include "hbrick/baselines/csr_bfs_baseline.hpp"
#include "hbrick/baselines/csr_dfs_baseline.hpp"
#include "hbrick/baselines/full_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_search_baseline.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/dag_reachability.hpp"
#include "hbrick/graph/dfs.hpp"
#include "maze_generator.hpp"
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

struct ScratchCapacities {
    std::size_t queue = 0U;
    std::size_t stack = 0U;
    std::size_t visited = 0U;
};

[[nodiscard]] ScratchCapacities captureCapacities(const hbrick::GraphSearchScratch& scratch) {
    return ScratchCapacities{
        scratch.queue().capacity(),
        scratch.stack().capacity(),
        scratch.visitedMark().capacity(),
    };
}

void expectCapacitiesUnchanged(
    const hbrick::GraphSearchScratch& scratch,
    const ScratchCapacities& before
) {
    EXPECT_EQ(scratch.queue().capacity(), before.queue);
    EXPECT_EQ(scratch.stack().capacity(), before.stack);
    EXPECT_EQ(scratch.visitedMark().capacity(), before.visited);
}

}  // namespace

TEST(HotPathAllocations, BfsAndDfsReachableDoNotResizeScratch) {
    const hbrick::PassableGrid maze = hbrick::test_support::generatePerfectMaze(
        hbrick::test_support::MazeParams{8U, 8U, 0xA110C8FEULL}
    );
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        maze,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0x515EEDULL, 0.15L, 0.25L}
    );

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    hbrick::GraphSearchScratch dfs_scratch(graph.numVertices());
    const ScratchCapacities bfs_before = captureCapacities(bfs_scratch);
    const ScratchCapacities dfs_before = captureCapacities(dfs_scratch);

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(hbrick::Bfs::reachable(graph, source, target, bfs_scratch));
            static_cast<void>(hbrick::Dfs::reachable(graph, source, target, dfs_scratch));
        }
    }

    expectCapacitiesUnchanged(bfs_scratch, bfs_before);
    expectCapacitiesUnchanged(dfs_scratch, dfs_before);
}

TEST(HotPathAllocations, DagReachabilityDoesNotResizeScratch) {
    const hbrick::CsrGraph dag = buildDiamondGraph();
    hbrick::GraphSearchScratch scratch(dag.numVertices());
    const ScratchCapacities before = captureCapacities(scratch);

    for (uint32_t source = 0U; source < dag.numVertices(); ++source) {
        for (uint32_t target = 0U; target < dag.numVertices(); ++target) {
            static_cast<void>(hbrick::DagReachability::reachable(
                dag,
                source,
                target,
                scratch
            ));
        }
    }

    expectCapacitiesUnchanged(scratch, before);
}

TEST(HotPathAllocations, BaselineQueriesDoNotResizeScratch) {
    const hbrick::PassableGrid maze = hbrick::test_support::generatePerfectMaze(
        hbrick::test_support::MazeParams{7U, 7U, 0xCAFEBABEULL}
    );
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        maze,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::GraphSearchScratch preprocess_scratch(graph.numVertices());
    hbrick::GraphSearchScratch query_scratch(graph.numVertices());
    const ScratchCapacities before = captureCapacities(query_scratch);

    hbrick::CsrBfsBaseline csr_bfs;
    hbrick::CsrDfsBaseline csr_dfs;
    hbrick::SccDagSearchBaseline scc_search;

    csr_bfs.preprocess(graph);
    csr_dfs.preprocess(graph);
    scc_search.preprocess(graph, preprocess_scratch);

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(csr_bfs.query(source, target, query_scratch));
            static_cast<void>(csr_dfs.query(source, target, query_scratch));
            static_cast<void>(scc_search.query(source, target, query_scratch));
        }
    }

    expectCapacitiesUnchanged(query_scratch, before);
}

TEST(HotPathAllocations, FullClosureQueryDoesNotAllocate) {
    const hbrick::CsrGraph graph = buildDiamondGraph();

    hbrick::FullClosureBaseline baseline;
    baseline.preprocess(graph, std::numeric_limits<uint64_t>::max());
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(baseline.query(source, target));
        }
    }
}

TEST(HotPathAllocations, SccDagClosureQueryDoesNotAllocate) {
    const hbrick::CsrGraph graph = buildDiamondGraph();

    hbrick::GraphSearchScratch preprocess_scratch(graph.numVertices());
    hbrick::SccDagClosureBaseline baseline;
    baseline.preprocess(graph, preprocess_scratch, std::numeric_limits<uint64_t>::max());
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(baseline.query(source, target));
        }
    }
}
