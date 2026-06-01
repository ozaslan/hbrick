#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "hbrick/graph/csr_graph_builder.hpp"

namespace {

std::vector<uint32_t> neighborsAsVector(const hbrick::CsrGraph& graph, const uint32_t vertex) {
    const std::span<const uint32_t> neighbors = graph.outNeighbors(vertex);
    return std::vector<uint32_t>(neighbors.begin(), neighbors.end());
}

}  // namespace

TEST(CsrGraph, EmptyGraphHasZeroEdges) {
    const hbrick::CsrGraph graph = hbrick::CsrGraphBuilder{4U}.build();

    EXPECT_EQ(graph.numVertices(), 4U);
    EXPECT_EQ(graph.numEdges(), 0U);
    EXPECT_TRUE(graph.outNeighbors(0U).empty());
    EXPECT_TRUE(graph.outNeighbors(3U).empty());
    EXPECT_TRUE(graph.outNeighbors(4U).empty());
}

TEST(CsrGraph, OutNeighborsReturnSortedTargets) {
    hbrick::CsrGraphBuilder builder{5U};
    builder.addEdge(1U, 4U);
    builder.addEdge(1U, 2U);
    builder.addEdge(1U, 3U);

    const hbrick::CsrGraph graph = builder.build();

    EXPECT_EQ(graph.numEdges(), 3U);
    EXPECT_EQ(neighborsAsVector(graph, 1U), (std::vector<uint32_t>{2U, 3U, 4U}));
    EXPECT_TRUE(graph.outNeighbors(0U).empty());
}

TEST(CsrGraph, DuplicateEdgesArePreserved) {
    hbrick::CsrGraphBuilder builder{2U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 1U);

    const hbrick::CsrGraph graph = builder.build();

    EXPECT_EQ(graph.numEdges(), 2U);
    EXPECT_EQ(neighborsAsVector(graph, 0U), (std::vector<uint32_t>{1U, 1U}));
}

TEST(CsrGraph, BuilderRejectsOutOfRangeEndpoints) {
    hbrick::CsrGraphBuilder builder{3U};

    EXPECT_THROW(builder.addEdge(3U, 0U), std::out_of_range);
    EXPECT_THROW(builder.addEdge(0U, 3U), std::out_of_range);
    EXPECT_EQ(builder.pendingEdgeCount(), 0U);
}

TEST(CsrGraph, OutNeighborsSpanPointsIntoStableStorage) {
    hbrick::CsrGraphBuilder builder{3U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 2U);

    const hbrick::CsrGraph graph = builder.build();
    const std::span<const uint32_t> first = graph.outNeighbors(0U);
    const std::span<const uint32_t> second = graph.outNeighbors(0U);

    ASSERT_EQ(first.size(), 2U);
    EXPECT_EQ(first.data(), second.data());
    EXPECT_EQ(first[0U], 1U);
    EXPECT_EQ(first[1U], 2U);
}

TEST(CsrGraph, KnownSmallGraphStructure) {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 2U);
    builder.addEdge(1U, 3U);
    builder.addEdge(2U, 3U);

    const hbrick::CsrGraph graph = builder.build();

    EXPECT_EQ(neighborsAsVector(graph, 0U), (std::vector<uint32_t>{1U, 2U}));
    EXPECT_EQ(neighborsAsVector(graph, 1U), (std::vector<uint32_t>{3U}));
    EXPECT_EQ(neighborsAsVector(graph, 2U), (std::vector<uint32_t>{3U}));
    EXPECT_TRUE(graph.outNeighbors(3U).empty());
}

TEST(CsrGraph, ClearRemovesPendingEdges) {
    hbrick::CsrGraphBuilder builder{2U};
    builder.addEdge(0U, 1U);
    builder.clear();

    EXPECT_EQ(builder.pendingEdgeCount(), 0U);
    EXPECT_EQ(builder.build().numEdges(), 0U);
}
