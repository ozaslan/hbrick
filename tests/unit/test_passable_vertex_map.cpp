#include <gtest/gtest.h>

#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/passable_vertex_map.hpp"
#include "hbrick/grid/maze_layout.hpp"

TEST(PassableVertexMap, CompactCountMatchesPassableCells) {
    hbrick::MazeLayout layout(4U, 3U, true);
    layout.setPassable(1U, 1U, false);
    layout.setPassable(3U, 0U, false);
    layout.setPassable(0U, 2U, false);

    const hbrick::PassableVertexMap map = hbrick::PassableVertexMap::fromLayout(layout);
    EXPECT_EQ(map.gridVertexCount(), 12U);
    EXPECT_EQ(map.compactVertexCount(), layout.passableCount());
    EXPECT_EQ(map.compactVertexCount(), 9U);

    EXPECT_EQ(map.toCompact(layout.vertexId({1U, 1U}).value), hbrick::kInvalidVertexId);
    EXPECT_NE(map.toCompact(layout.vertexId({0U, 0U}).value), hbrick::kInvalidVertexId);
}

TEST(PassableVertexMap, InduceDropsBlockedCellsAndPreservesReachability) {
    hbrick::MazeLayout layout(4U, 3U, true);
    layout.setPassable(1U, 1U, false);
    layout.setPassable(2U, 1U, false);

    const hbrick::DirectedGridGraph grid = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    EXPECT_EQ(grid.numVertices(), 12U);

    const hbrick::PassableVertexMap map = hbrick::PassableVertexMap::fromLayout(layout);
    const hbrick::CsrGraph compact = hbrick::inducePassableCsr(grid, map);
    EXPECT_EQ(compact.numVertices(), layout.passableCount());
    EXPECT_EQ(compact.numEdges(), grid.numEdges());

    hbrick::GraphSearchScratch grid_scratch(grid.numVertices());
    hbrick::GraphSearchScratch compact_scratch(compact.numVertices());

    const std::vector<uint32_t> passable = hbrick::collectPassableGridVertices(layout);
    ASSERT_EQ(passable.size(), compact.numVertices());

    for (const uint32_t grid_source : passable) {
        for (const uint32_t grid_target : passable) {
            const hbrick::ReachabilityAnswer grid_answer = hbrick::Bfs::reachable(
                grid.csrGraph(),
                grid_source,
                grid_target,
                grid_scratch
            );
            const hbrick::ReachabilityAnswer compact_answer = hbrick::Bfs::reachable(
                compact,
                map.toCompact(grid_source),
                map.toCompact(grid_target),
                compact_scratch
            );
            EXPECT_EQ(grid_answer, compact_answer)
                << "grid " << grid_source << " -> " << grid_target;
        }
    }
}

TEST(PassableVertexMap, IsolatedPassableCellRemainsAVertex) {
    hbrick::MazeLayout layout(3U, 1U, false);
    layout.setPassable(0U, 0U, true);
    layout.setPassable(2U, 0U, true);

    const hbrick::DirectedGridGraph grid = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::PassableVertexMap map = hbrick::PassableVertexMap::fromLayout(layout);
    const hbrick::CsrGraph compact = hbrick::inducePassableCsr(grid, map);

    EXPECT_EQ(compact.numVertices(), 2U);
    EXPECT_EQ(compact.numEdges(), 0U);
}
