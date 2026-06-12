#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/dfs.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace hbrick {

class GraphSearchScratchOverflowTest {
public:
    static void setCurrentMark(GraphSearchScratch& scratch, const uint32_t mark) {
        scratch.current_mark_ = mark;
    }

    static void setVisitedMark(
        GraphSearchScratch& scratch,
        const std::size_t index,
        const uint32_t value
    ) {
        scratch.visited_mark_[index] = value;
    }
};

}  // namespace hbrick

namespace {

hbrick::CsrGraph buildDiamondGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 2U);
    builder.addEdge(1U, 3U);
    builder.addEdge(2U, 3U);
    return builder.build();
}

hbrick::ReachabilityAnswer bfsReachable(
    const hbrick::CsrGraph& graph,
    const uint32_t source,
    const uint32_t target,
    hbrick::GraphSearchScratch& scratch
) {
    return hbrick::Bfs::reachable(graph, source, target, scratch);
}

hbrick::ReachabilityAnswer dfsReachable(
    const hbrick::CsrGraph& graph,
    const uint32_t source,
    const uint32_t target,
    hbrick::GraphSearchScratch& scratch
) {
    return hbrick::Dfs::reachable(graph, source, target, scratch);
}

void expectAgreementOnAllPairs(const hbrick::CsrGraph& graph) {
    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    hbrick::GraphSearchScratch dfs_scratch(graph.numVertices());

    for (uint32_t source = 0; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer bfs_answer = bfsReachable(graph, source, target, bfs_scratch);
            const hbrick::ReachabilityAnswer dfs_answer = dfsReachable(graph, source, target, dfs_scratch);
            EXPECT_EQ(bfs_answer, dfs_answer) << "source=" << source << " target=" << target;
        }
    }
}

}  // namespace

TEST(BfsDfs, AgreeOnKnownGraph) {
    const hbrick::CsrGraph graph = buildDiamondGraph();
    hbrick::GraphSearchScratch scratch(graph.numVertices());

    EXPECT_EQ(bfsReachable(graph, 0U, 3U, scratch), hbrick::ReachabilityAnswer::Reachable);
    EXPECT_EQ(dfsReachable(graph, 0U, 3U, scratch), hbrick::ReachabilityAnswer::Reachable);
    EXPECT_EQ(bfsReachable(graph, 3U, 0U, scratch), hbrick::ReachabilityAnswer::Unreachable);
    EXPECT_EQ(dfsReachable(graph, 3U, 0U, scratch), hbrick::ReachabilityAnswer::Unreachable);

    expectAgreementOnAllPairs(graph);
}

TEST(BfsDfs, AgreeOnRandomSmallGridGraphs) {
    const hbrick::MazeLayout grid(4U, 3U);
    const hbrick::DirectedGridGraph directed = hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0xDEADBEEFULL, 0.2L, 0.25L}
    );

    expectAgreementOnAllPairs(directed.csrGraph());
}

TEST(BfsDfs, ReachableDoesNotResizeScratchBuffers) {
    const hbrick::CsrGraph graph = buildDiamondGraph();
    hbrick::GraphSearchScratch scratch(graph.numVertices());

    const std::size_t queue_capacity_before = scratch.queue().capacity();
    const std::size_t stack_capacity_before = scratch.stack().capacity();
    const std::size_t visited_capacity_before = scratch.visitedMark().capacity();

    for (uint32_t source = 0; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0; target < graph.numVertices(); ++target) {
            static_cast<void>(bfsReachable(graph, source, target, scratch));
            static_cast<void>(dfsReachable(graph, source, target, scratch));
        }
    }

    EXPECT_GE(scratch.queue().capacity(), queue_capacity_before);
    EXPECT_GE(scratch.stack().capacity(), stack_capacity_before);
    EXPECT_GE(scratch.visitedMark().capacity(), visited_capacity_before);
    EXPECT_EQ(scratch.queue().capacity(), queue_capacity_before);
    EXPECT_EQ(scratch.stack().capacity(), stack_capacity_before);
    EXPECT_EQ(scratch.visitedMark().capacity(), visited_capacity_before);
}

TEST(GraphSearchScratch, NextMarkOverflowClearsVisitedMarks) {
    hbrick::GraphSearchScratch scratch(3U);
    hbrick::GraphSearchScratchOverflowTest::setVisitedMark(scratch, 0U, 7U);
    hbrick::GraphSearchScratchOverflowTest::setVisitedMark(scratch, 1U, 9U);
    hbrick::GraphSearchScratchOverflowTest::setCurrentMark(
        scratch,
        std::numeric_limits<uint32_t>::max()
    );

    const uint32_t mark = scratch.nextMark();
    EXPECT_EQ(mark, 1U);
    EXPECT_EQ(scratch.visitedMark()[0U], 0U);
    EXPECT_EQ(scratch.visitedMark()[1U], 0U);
    EXPECT_EQ(scratch.visitedMark()[2U], 0U);
}

TEST(GraphSearchScratch, ResetForGraphPreallocatesBuffers) {
    hbrick::GraphSearchScratch scratch;
    scratch.resetForGraph(8U);

    EXPECT_EQ(scratch.visitedMark().size(), 8U);
    EXPECT_GE(scratch.queue().capacity(), 8U);
    EXPECT_GE(scratch.stack().capacity(), 8U);
    EXPECT_GT(scratch.memoryBytes(), 0U);
}
