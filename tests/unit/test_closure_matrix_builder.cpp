#include <gtest/gtest.h>

#include <stdexcept>

#include "hbrick/baselines/closure_matrix_builder.hpp"
#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"

namespace {

hbrick::CsrGraph buildSmallGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 2U);
    builder.addEdge(1U, 3U);
    builder.addEdge(2U, 3U);
    return builder.build();
}

}  // namespace

TEST(ClosureMatrixBuilder, MemoryEstimateMatchesFormula) {
    EXPECT_EQ(hbrick::ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes(0U), 0U);
    EXPECT_EQ(hbrick::ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes(1U), 8U);
    EXPECT_EQ(hbrick::ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes(64U), 512U);
    EXPECT_EQ(hbrick::ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes(65U), 1040U);
    EXPECT_EQ(hbrick::ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes(100U), 1600U);
}

TEST(ClosureMatrixBuilder, CanAllocateRejectsLargeGraphByPolicy) {
    const uint32_t num_vertices = 65U;
    const uint64_t estimate =
        hbrick::ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes(num_vertices);

    EXPECT_TRUE(hbrick::ClosureMatrixBuilder::canAllocateReflexiveAdjacency(
        num_vertices,
        estimate
    ));
    EXPECT_FALSE(hbrick::ClosureMatrixBuilder::canAllocateReflexiveAdjacency(
        num_vertices,
        estimate - 1U
    ));
}

TEST(ClosureMatrixBuilder, BuildSetsIdentityAndEdgeEntries) {
    const hbrick::CsrGraph graph = buildSmallGraph();
    const hbrick::BitMatrix relation =
        hbrick::ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(graph, 4096U);

    EXPECT_EQ(relation.numRows(), 4U);
    EXPECT_EQ(relation.numCols(), 4U);

    for (uint32_t vertex = 0; vertex < 4U; ++vertex) {
        EXPECT_TRUE(relation.test(vertex, vertex)) << "vertex=" << vertex;
    }

    EXPECT_TRUE(relation.test(0U, 1U));
    EXPECT_TRUE(relation.test(0U, 2U));
    EXPECT_TRUE(relation.test(1U, 3U));
    EXPECT_TRUE(relation.test(2U, 3U));
    EXPECT_FALSE(relation.test(3U, 0U));
}

TEST(ClosureMatrixBuilder, SmallGraphReflexiveAdjacencyMatchesEdges) {
    const hbrick::CsrGraph graph = buildSmallGraph();
    const hbrick::BitMatrix relation =
        hbrick::ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(graph, 4096U);

    for (const hbrick::Edge32& edge : graph.edges()) {
        EXPECT_TRUE(relation.test(edge.from, edge.to));
    }
}

TEST(ClosureMatrixBuilder, BuildThrowsWhenPolicyLimitExceeded) {
    const hbrick::CsrGraph graph = buildSmallGraph();
    const uint64_t estimate = hbrick::ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes(
        graph.numVertices()
    );
    const uint64_t too_small_limit = estimate - 1U;

    EXPECT_FALSE(hbrick::ClosureMatrixBuilder::canAllocateReflexiveAdjacency(
        graph.numVertices(),
        too_small_limit
    ));
    EXPECT_THROW(
        hbrick::ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(graph, too_small_limit),
        std::runtime_error
    );
}

TEST(ClosureMatrixBuilder, KleeneTruncatedClosureMatchesWarshallOracle) {
    const hbrick::CsrGraph graph = buildSmallGraph();
    constexpr uint64_t kBudget = 4096U;

    hbrick::BitMatrix kleene =
        hbrick::ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(graph, kBudget);
    hbrick::BitMatrix scratch{};
    hbrick::ClosureMatrixBuilder::transitiveClosureKleeneTruncatedInPlace(
        kleene,
        graph,
        &scratch
    );

    hbrick::BitMatrix warshall =
        hbrick::ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(graph, kBudget);
    hbrick::ClosureMatrixBuilder::transitiveClosureWarshallOracleInPlace(warshall);

    EXPECT_TRUE(hbrick::bitMatricesEqual(kleene, warshall));
}

TEST(ClosureMatrixBuilder, KleeneTruncatedClosureMatchesWarshallOnDisconnectedGraph) {
    hbrick::CsrGraphBuilder builder{8U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 2U);
    builder.addEdge(4U, 5U);
    const hbrick::CsrGraph graph = builder.build();
    constexpr uint64_t kBudget = 4096U;

    hbrick::BitMatrix kleene =
        hbrick::ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(graph, kBudget);
    hbrick::BitMatrix scratch{};
    hbrick::ClosureMatrixBuilder::transitiveClosureKleeneTruncatedInPlace(
        kleene,
        graph,
        &scratch
    );

    hbrick::BitMatrix warshall =
        hbrick::ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(graph, kBudget);
    hbrick::ClosureMatrixBuilder::transitiveClosureWarshallOracleInPlace(warshall);

    EXPECT_TRUE(hbrick::bitMatricesEqual(kleene, warshall));
}
