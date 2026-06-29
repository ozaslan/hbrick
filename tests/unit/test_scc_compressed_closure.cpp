#include <gtest/gtest.h>

#include <limits>
#include <random>

#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/scc_compressed_closure.hpp"
#include "hbrick/tile/tile_closure_util.hpp"

TEST(SccCompressedClosure, ExpandMarksIntraAndInterComponentReachability) {
    hbrick::CsrGraphBuilder builder{6U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 0U);
    builder.addEdge(1U, 2U);
    builder.addEdge(2U, 3U);
    builder.addEdge(3U, 4U);
    builder.addEdge(4U, 5U);
    builder.addEdge(5U, 4U);
    const hbrick::CsrGraph graph = builder.build();

    hbrick::GraphSearchScratch scratch{graph.numVertices()};
    const hbrick::SccDecomposition decomposition =
        hbrick::SccDecomposition::compute(graph, scratch);
    const hbrick::SccCompressedReflexiveAdjacency compressed =
        hbrick::buildSccCompressedReflexiveAdjacency(graph, decomposition);

    EXPECT_EQ(compressed.decomposition.numComponents(), 4U);
    EXPECT_EQ(compressed.component_adjacency.numRows(), 4U);

    hbrick::BitMatrix component_closure = compressed.component_adjacency;
    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(component_closure);

    hbrick::BitMatrix vertex_closure;
    hbrick::expandComponentClosureToVertexClosure(
        compressed.decomposition,
        component_closure,
        vertex_closure
    );

    EXPECT_TRUE(vertex_closure.test(0U, 1U));
    EXPECT_TRUE(vertex_closure.test(1U, 0U));
    EXPECT_TRUE(vertex_closure.test(0U, 3U));
    EXPECT_TRUE(vertex_closure.test(4U, 5U));
    EXPECT_FALSE(vertex_closure.test(5U, 0U));
}

TEST(SccCompressedClosure, VertexReachabilityMapsThroughComponents) {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 2U);
    builder.addEdge(2U, 3U);
    const hbrick::CsrGraph graph = builder.build();

    hbrick::GraphSearchScratch scratch{graph.numVertices()};
    const hbrick::SccDecomposition decomposition =
        hbrick::SccDecomposition::compute(graph, scratch);
    const hbrick::SccCompressedReflexiveAdjacency compressed =
        hbrick::buildSccCompressedReflexiveAdjacency(graph, decomposition);

    hbrick::BitMatrix component_closure = compressed.component_adjacency;
    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(component_closure);

    EXPECT_TRUE(hbrick::vertexReachableInComponentClosure(
        compressed.decomposition,
        component_closure,
        0U,
        3U
    ));
    EXPECT_FALSE(hbrick::vertexReachableInComponentClosure(
        compressed.decomposition,
        component_closure,
        3U,
        0U
    ));
}

TEST(SccCompressedClosure, KleeneMatchesWarshallOnCompressedGraphs) {
    hbrick::CsrGraphBuilder builder{20U};
    for (uint32_t pair = 0U; pair < 10U; ++pair) {
        const uint32_t lhs = pair * 2U;
        const uint32_t rhs = lhs + 1U;
        builder.addEdge(lhs, rhs);
        builder.addEdge(rhs, lhs);
        if (pair + 1U < 10U) {
            builder.addEdge(rhs, lhs + 2U);
        }
    }
    const hbrick::CsrGraph graph = builder.build();
    constexpr uint64_t kBudget = 65536U;

    hbrick::BitMatrix compressed_path =
        hbrick::buildTileReflexiveAdjacencyOrThrow(graph, kBudget);
    hbrick::GraphSearchScratch scratch{graph.numVertices()};
    EXPECT_TRUE(hbrick::transitiveClosureKleeneSccCompressedInPlace(
        compressed_path,
        graph,
        scratch
    ));

    hbrick::BitMatrix warshall =
        hbrick::buildTileReflexiveAdjacencyOrThrow(graph, kBudget);
    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(warshall);

    EXPECT_TRUE(hbrick::bitMatricesEqual(compressed_path, warshall));
}

TEST(SccCompressedClosure, KleeneMatchesWarshallOnRandomGraphs) {
    std::mt19937_64 rng{0xC0FFEEULL};
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    const uint32_t sizes[] = {4U, 8U, 16U, 32U, 64U, 96U};
    const double densities[] = {0.05, 0.15, 0.35};

    for (const uint32_t num_vertices : sizes) {
        for (const double density : densities) {
            hbrick::CsrGraphBuilder builder{num_vertices};
            for (uint32_t from = 0U; from < num_vertices; ++from) {
                for (uint32_t to = 0U; to < num_vertices; ++to) {
                    if (from != to && coin(rng) < density) {
                        builder.addEdge(from, to);
                    }
                }
            }
            const hbrick::CsrGraph graph = builder.build();
            constexpr uint64_t kBudget = 1U << 24U;

            hbrick::BitMatrix kleene =
                hbrick::buildTileReflexiveAdjacencyOrThrow(graph, kBudget);
            hbrick::BitMatrix scratch{};
            hbrick::GraphSearchScratch scc_scratch{num_vertices};
            (void)hbrick::transitiveClosureKleeneSccCompressedInPlace(
                kleene,
                graph,
                scc_scratch,
                &scratch
            );

            hbrick::BitMatrix warshall =
                hbrick::buildTileReflexiveAdjacencyOrThrow(graph, kBudget);
            hbrick::BooleanClosure::transitiveClosureWarshallInPlace(warshall);

            EXPECT_TRUE(hbrick::bitMatricesEqual(kleene, warshall))
                << "M=" << num_vertices << " density=" << density;
        }
    }
}

TEST(SccCompressedClosure, ReflexiveMatrixPathMatchesCsrPath) {
    hbrick::CsrGraphBuilder builder{8U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 0U);
    builder.addEdge(1U, 2U);
    builder.addEdge(2U, 3U);
    const hbrick::CsrGraph graph = builder.build();
    constexpr uint64_t kBudget = 4096U;

    hbrick::BitMatrix csr_path =
        hbrick::buildTileReflexiveAdjacencyOrThrow(graph, kBudget);
    hbrick::BitMatrix matrix_path =
        hbrick::buildTileReflexiveAdjacencyOrThrow(graph, kBudget);
    hbrick::BitMatrix scratch{};

    hbrick::GraphSearchScratch csr_scratch{graph.numVertices()};
    EXPECT_TRUE(hbrick::transitiveClosureKleeneSccCompressedInPlace(
        csr_path,
        graph,
        csr_scratch,
        &scratch
    ));

    hbrick::GraphSearchScratch matrix_scratch{graph.numVertices()};
    EXPECT_TRUE(hbrick::transitiveClosureKleeneSccCompressedInPlace(
        matrix_path,
        matrix_scratch,
        &scratch
    ));

    EXPECT_TRUE(hbrick::bitMatricesEqual(csr_path, matrix_path));
}
