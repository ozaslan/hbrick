#include <gtest/gtest.h>

#include <limits>

#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/condensation_graph.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/dag_reachability.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/scc_decomposition.hpp"
#include "maze_generator.hpp"
#include "reachability_oracle.hpp"

namespace {

hbrick::CsrGraph buildDiamondDag() {
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

void expectDagReachabilityMatchesBfsOnAllPairs(const hbrick::CsrGraph& dag) {
    hbrick::GraphSearchScratch bfs_scratch(dag.numVertices());
    hbrick::GraphSearchScratch dag_scratch(dag.numVertices());

    for (uint32_t source = 0U; source < dag.numVertices(); ++source) {
        for (uint32_t target = 0U; target < dag.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer expected = hbrick::Bfs::reachable(
                dag,
                source,
                target,
                bfs_scratch
            );
            const hbrick::ReachabilityAnswer actual = hbrick::DagReachability::reachable(
                dag,
                source,
                target,
                dag_scratch
            );
            EXPECT_EQ(actual, expected) << "source=" << source << " target=" << target;
        }
    }
}

}  // namespace

TEST(DagReachability, MatchesBfsOnKnownDag) {
    expectDagReachabilityMatchesBfsOnAllPairs(buildDiamondDag());
}

TEST(DagReachability, SameComponentIsReachableWithoutSearch) {
    const hbrick::CsrGraph dag = buildDiamondDag();
    hbrick::GraphSearchScratch scratch(dag.numVertices());

    EXPECT_EQ(
        hbrick::DagReachability::reachable(dag, 2U, 2U, scratch),
        hbrick::ReachabilityAnswer::Reachable
    );
}

TEST(DagReachability, MatchesBfsOnCondensationDag) {
    const hbrick::CsrGraph graph = buildCycleGraph();
    hbrick::GraphSearchScratch scratch(graph.numVertices());

    const hbrick::SccDecomposition decomposition = hbrick::SccDecomposition::compute(graph, scratch);
    const hbrick::CondensationGraph condensation =
        hbrick::CondensationGraph::fromGraph(graph, decomposition);

    expectDagReachabilityMatchesBfsOnAllPairs(condensation.dag());
}

TEST(DagReachability, MatchesBfsOnAllPairsForAcyclicMazeDag) {
    const hbrick::MazeLayout maze = hbrick::test_support::generatePerfectMaze(
        hbrick::test_support::MazeParams{12U, 10U, 0xDABBAD00ULL}
    );
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        maze,
        hbrick::GridEdgeConversionMode::AcyclicEastSouth
    );

    expectDagReachabilityMatchesBfsOnAllPairs(graph);
}

TEST(DagReachability, MatchesBfsOnCondensationOfCyclicMaze) {
    const hbrick::MazeLayout maze = hbrick::test_support::generateMazeWithExtraPassages(
        hbrick::test_support::MazeParams{10U, 8U, 0xFACEFEEDULL},
        0x0A115A115ULL,
        24U
    );
    const hbrick::CsrGraph maze_graph = hbrick::test_support::buildGridGraph(
        maze,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0x0DD515ULL, 0.12L, 0.28L}
    );

    hbrick::GraphSearchScratch scratch(maze_graph.numVertices());
    const hbrick::SccDecomposition maze_decomposition =
        hbrick::SccDecomposition::compute(maze_graph, scratch);
    const hbrick::CondensationGraph maze_condensation =
        hbrick::CondensationGraph::fromGraph(maze_graph, maze_decomposition);

    expectDagReachabilityMatchesBfsOnAllPairs(maze_condensation.dag());
}

TEST(DagReachability, QueryDoesNotResizeScratchBuffers) {
    const hbrick::CsrGraph dag = buildDiamondDag();
    hbrick::GraphSearchScratch scratch(dag.numVertices());

    const std::size_t queue_capacity_before = scratch.queue().capacity();
    const std::size_t stack_capacity_before = scratch.stack().capacity();
    const std::size_t visited_capacity_before = scratch.visitedMark().capacity();

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

    EXPECT_EQ(scratch.queue().capacity(), queue_capacity_before);
    EXPECT_EQ(scratch.stack().capacity(), stack_capacity_before);
    EXPECT_EQ(scratch.visitedMark().capacity(), visited_capacity_before);
}
