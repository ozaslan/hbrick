#include <gtest/gtest.h>

#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "maze_generator.hpp"

namespace {

void expectUndirectedConnectivity(const hbrick::PassableGrid& grid) {
    uint32_t start_vertex = hbrick::kInvalidVertexId;
    for (uint32_t y = 0U; y < grid.height(); ++y) {
        for (uint32_t x = 0U; x < grid.width(); ++x) {
            if (grid.isPassable(x, y)) {
                start_vertex = grid.vertexId(hbrick::GridCoord{x, y}).value;
                break;
            }
        }
        if (start_vertex != hbrick::kInvalidVertexId) {
            break;
        }
    }

    ASSERT_NE(start_vertex, hbrick::kInvalidVertexId);

    hbrick::CsrGraphBuilder builder{grid.numVertices()};
    for (uint32_t y = 0U; y < grid.height(); ++y) {
        for (uint32_t x = 0U; x < grid.width(); ++x) {
            const hbrick::GridCoord from{x, y};
            if (!grid.isPassable(from)) {
                continue;
            }

            const hbrick::GridCoord east{x + 1U, y};
            if (grid.contains(east) && grid.isPassable(east)) {
                const uint32_t from_vertex = grid.vertexId(from).value;
                const uint32_t to_vertex = grid.vertexId(east).value;
                builder.addEdge(from_vertex, to_vertex);
                builder.addEdge(to_vertex, from_vertex);
            }

            const hbrick::GridCoord south{x, y + 1U};
            if (grid.contains(south) && grid.isPassable(south)) {
                const uint32_t from_vertex = grid.vertexId(from).value;
                const uint32_t to_vertex = grid.vertexId(south).value;
                builder.addEdge(from_vertex, to_vertex);
                builder.addEdge(to_vertex, from_vertex);
            }
        }
    }

    const hbrick::CsrGraph graph = builder.build();
    hbrick::GraphSearchScratch scratch(graph.numVertices());

    for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
        if (!grid.isPassable(hbrick::VertexId{target})) {
            continue;
        }

        EXPECT_EQ(
            hbrick::Bfs::reachable(graph, start_vertex, target, scratch),
            hbrick::ReachabilityAnswer::Reachable
        ) << "target=" << target;
    }
}

}  // namespace

TEST(MazeGenerator, PerfectMazeHasExpectedDimensionsAndConnectivity) {
    const hbrick::PassableGrid maze = hbrick::test_support::generatePerfectMaze(
        hbrick::test_support::MazeParams{8U, 6U, 0x8A2E5EED1ULL}
    );

    EXPECT_EQ(maze.width(), 17U);
    EXPECT_EQ(maze.height(), 13U);
    EXPECT_GT(maze.passableCount(), 0U);
    expectUndirectedConnectivity(maze);
}

TEST(MazeGenerator, SameSeedProducesIdenticalMaze) {
    const hbrick::test_support::MazeParams params{9U, 7U, 0x8A2E5EED2ULL};
    const hbrick::PassableGrid first = hbrick::test_support::generatePerfectMaze(params);
    const hbrick::PassableGrid second = hbrick::test_support::generatePerfectMaze(params);

    EXPECT_EQ(first.width(), second.width());
    EXPECT_EQ(first.height(), second.height());
    EXPECT_EQ(first.passableCount(), second.passableCount());

    for (uint32_t y = 0U; y < first.height(); ++y) {
        for (uint32_t x = 0U; x < first.width(); ++x) {
            EXPECT_EQ(first.isPassable(x, y), second.isPassable(x, y)) << "x=" << x << " y=" << y;
        }
    }
}

TEST(MazeGenerator, ExtraPassagesIncreasePassableCells) {
    const hbrick::test_support::MazeParams params{10U, 10U, 0x8A2E5EED3ULL};
    const hbrick::PassableGrid perfect = hbrick::test_support::generatePerfectMaze(params);
    const hbrick::PassableGrid cyclic = hbrick::test_support::generateMazeWithExtraPassages(
        params,
        0x0A115A115ULL,
        20U
    );

    EXPECT_GE(cyclic.passableCount(), perfect.passableCount());
    expectUndirectedConnectivity(cyclic);
}
