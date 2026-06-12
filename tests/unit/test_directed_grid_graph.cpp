#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/edge32.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace {

std::vector<hbrick::Edge32> buildAcyclicEdges(const hbrick::MazeLayout& grid) {
    return hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::AcyclicEastSouth
    ).edges();
}

std::vector<hbrick::Edge32> buildBidirectionalEdges(const hbrick::MazeLayout& grid) {
    return hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    ).edges();
}

std::vector<hbrick::Edge32> buildRandomEdges(
    const hbrick::MazeLayout& grid,
    const hbrick::RandomAsymmetricParams& params
) {
    return hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        params
    ).edges();
}

}  // namespace

TEST(DirectedGridGraph, OutNeighborsMatchesCsrStorage) {
    const hbrick::MazeLayout grid(3U, 2U);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::AcyclicEastSouth
    );

    EXPECT_EQ(graph.width(), 3U);
    EXPECT_EQ(graph.height(), 2U);
    EXPECT_EQ(graph.numVertices(), 6U);
    EXPECT_EQ(graph.coordFromVertex(4U), hbrick::GridCoord(1U, 1U));
    EXPECT_EQ(graph.vertexFromCoord(hbrick::GridCoord{2U, 0U}), 2U);

    const std::span<const uint32_t> neighbors = graph.outNeighbors(0U);
    ASSERT_EQ(neighbors.size(), 2U);
    EXPECT_EQ(neighbors[0U], 1U);
    EXPECT_EQ(neighbors[1U], 3U);
}

TEST(DirectedGridGraph, AcyclicEastSouthIsDeterministicAndRowMajor) {
    const hbrick::MazeLayout grid(3U, 2U);
    const std::vector<hbrick::Edge32> first = buildAcyclicEdges(grid);
    const std::vector<hbrick::Edge32> second = buildAcyclicEdges(grid);

    ASSERT_EQ(first.size(), 7U);
    EXPECT_EQ(first, second);

    EXPECT_EQ(first[0U], hbrick::Edge32(0U, 1U));
    EXPECT_EQ(first[1U], hbrick::Edge32(0U, 3U));
    EXPECT_EQ(first[2U], hbrick::Edge32(1U, 2U));
    EXPECT_EQ(first[3U], hbrick::Edge32(1U, 4U));
    EXPECT_EQ(first[4U], hbrick::Edge32(2U, 5U));
    EXPECT_EQ(first[5U], hbrick::Edge32(3U, 4U));
    EXPECT_EQ(first[6U], hbrick::Edge32(4U, 5U));
}

TEST(DirectedGridGraph, BidirectionalModeDoesNotUseRng) {
    const hbrick::MazeLayout grid(2U, 2U);
    const std::vector<hbrick::Edge32> first = buildBidirectionalEdges(grid);
    const std::vector<hbrick::Edge32> second = buildBidirectionalEdges(grid);

    ASSERT_EQ(first.size(), 8U);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first[0U], hbrick::Edge32(0U, 1U));
    EXPECT_EQ(first[1U], hbrick::Edge32(0U, 2U));
    EXPECT_EQ(first[2U], hbrick::Edge32(1U, 0U));
    EXPECT_EQ(first[3U], hbrick::Edge32(1U, 3U));
    EXPECT_EQ(first[4U], hbrick::Edge32(2U, 0U));
    EXPECT_EQ(first[5U], hbrick::Edge32(2U, 3U));
    EXPECT_EQ(first[6U], hbrick::Edge32(3U, 1U));
    EXPECT_EQ(first[7U], hbrick::Edge32(3U, 2U));
}

TEST(DirectedGridGraph, SameSeedProducesIdenticalRandomGraph) {
    const hbrick::MazeLayout grid(4U, 3U);
    const hbrick::RandomAsymmetricParams params{
        0x123456789ABCDEF0ULL,
        0.25L,
        0.25L,
    };

    const std::vector<hbrick::Edge32> first = buildRandomEdges(grid, params);
    const std::vector<hbrick::Edge32> second = buildRandomEdges(grid, params);

    EXPECT_EQ(first, second);
    EXPECT_FALSE(first.empty());
}

TEST(DirectedGridGraph, DifferentSeedUsuallyProducesDifferentGraph) {
    const hbrick::MazeLayout grid(5U, 4U);
    const hbrick::RandomAsymmetricParams first_params{1ULL, 0.2L, 0.3L};
    const hbrick::RandomAsymmetricParams second_params{2ULL, 0.2L, 0.3L};

    const std::vector<hbrick::Edge32> first = buildRandomEdges(grid, first_params);
    const std::vector<hbrick::Edge32> second = buildRandomEdges(grid, second_params);

    EXPECT_NE(first, second);
}

TEST(DirectedGridGraph, RandomProcedureUsesIntegerThresholdsDocumentedInTest) {
    const hbrick::MazeLayout grid(2U, 1U);
    const hbrick::RandomAsymmetricParams always_bidirectional{99ULL, 1.0L, 0.0L};
    const hbrick::RandomAsymmetricParams always_one_way{99ULL, 0.0L, 1.0L};
    const hbrick::RandomAsymmetricParams never_add{99ULL, 0.0L, 0.0L};

    const std::vector<hbrick::Edge32> bidirectional = buildRandomEdges(grid, always_bidirectional);
    const std::vector<hbrick::Edge32> one_way = buildRandomEdges(grid, always_one_way);
    const std::vector<hbrick::Edge32> none = buildRandomEdges(grid, never_add);

    EXPECT_EQ(bidirectional, (std::vector<hbrick::Edge32>{hbrick::Edge32(0U, 1U), hbrick::Edge32(1U, 0U)}));
    EXPECT_EQ(one_way.size(), 1U);
    EXPECT_TRUE(one_way[0U] == hbrick::Edge32(0U, 1U) || one_way[0U] == hbrick::Edge32(1U, 0U));
    EXPECT_TRUE(none.empty());
}

TEST(DirectedGridGraph, ImpassableCellsRemoveIncidentPairs) {
    hbrick::MazeLayout grid(3U, 2U);
    grid.setPassable(hbrick::GridCoord{1U, 0U}, false);

    const std::vector<hbrick::Edge32> edges = buildAcyclicEdges(grid);

    ASSERT_EQ(edges.size(), 4U);
    EXPECT_EQ(edges[0U], hbrick::Edge32(0U, 3U));
    EXPECT_EQ(edges[1U], hbrick::Edge32(2U, 5U));
    EXPECT_EQ(edges[2U], hbrick::Edge32(3U, 4U));
    EXPECT_EQ(edges[3U], hbrick::Edge32(4U, 5U));
}
