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

TEST(SccCompressedClosure, CompressionThresholdBypassesSccWhenRatioExceedsPoint85) {
    // 20 vertices with a single 2-vertex cycle -> 19 components (ratio 0.95 > 0.85)
    EXPECT_FALSE(hbrick::shouldUseSccCompressedClosure(20U, 19U));
    EXPECT_TRUE(hbrick::shouldUseSccCompressedClosure(20U, 16U));

    hbrick::CsrGraphBuilder builder{20U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 0U);
    for (uint32_t i = 1U; i + 1U < 20U; ++i) {
        builder.addEdge(i, i + 1U);
    }
    const hbrick::CsrGraph graph = builder.build();
    constexpr uint64_t kBudget = 16384U;

    hbrick::BitMatrix matrix = hbrick::buildTileReflexiveAdjacencyOrThrow(graph, kBudget);
    hbrick::GraphSearchScratch scratch{graph.numVertices()};
    // Must return false because it bypassed SCC compression due to high C/V ratio
    EXPECT_FALSE(hbrick::transitiveClosureKleeneSccCompressedInPlace(
        matrix,
        graph,
        scratch
    ));

    hbrick::BitMatrix warshall = hbrick::buildTileReflexiveAdjacencyOrThrow(graph, kBudget);
    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(warshall);
    EXPECT_TRUE(hbrick::bitMatricesEqual(matrix, warshall));
}

TEST(SccCompressedClosure, ReverseTopologicalDpHandlesDeepChainsAndDiamonds) {
    // 10 clusters of 4 vertices each (40 vertices total) forming a DAG of cycles
    constexpr uint32_t kClusters = 10U;
    constexpr uint32_t kClusterSize = 4U;
    constexpr uint32_t kTotalVertices = kClusters * kClusterSize;

    hbrick::CsrGraphBuilder builder{kTotalVertices};
    for (uint32_t c = 0U; c < kClusters; ++c) {
        const uint32_t base = c * kClusterSize;
        // Cycle within cluster
        for (uint32_t i = 0U; i < kClusterSize; ++i) {
            builder.addEdge(base + i, base + ((i + 1U) % kClusterSize));
        }
        // Forward DAG edges
        if (c + 1U < kClusters) {
            builder.addEdge(base + (kClusterSize - 1U), (c + 1U) * kClusterSize);
        }
        if (c + 2U < kClusters) {
            builder.addEdge(base + (kClusterSize - 1U), (c + 2U) * kClusterSize);
        }
    }

    const hbrick::CsrGraph graph = builder.build();
    constexpr uint64_t kBudget = 65536U;

    hbrick::BitMatrix closure = hbrick::buildTileReflexiveAdjacencyOrThrow(graph, kBudget);
    hbrick::GraphSearchScratch scratch{graph.numVertices()};
    EXPECT_TRUE(hbrick::transitiveClosureKleeneSccCompressedInPlace(
        closure,
        graph,
        scratch
    ));

    hbrick::BitMatrix warshall = hbrick::buildTileReflexiveAdjacencyOrThrow(graph, kBudget);
    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(warshall);
    EXPECT_TRUE(hbrick::bitMatricesEqual(closure, warshall));
}

TEST(SccCompressedClosure, DagClosureKahnFallbackOnReversedTopologicalOrdering) {
    // Construct a DAG where edges go from higher index to lower index (3 -> 2 -> 1 -> 0)
    // This violates natural topological ordering (u < v) and triggers Kahn's fallback.
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(3U, 2U);
    builder.addEdge(2U, 1U);
    builder.addEdge(1U, 0U);
    const hbrick::CsrGraph dag = builder.build();

    hbrick::BitMatrix closure(4U, 4U);
    for (uint32_t u = 0U; u < 4U; ++u) {
        closure.set(u, u);
        for (const uint32_t v : dag.outNeighbors(u)) {
            closure.set(u, v);
        }
    }

    hbrick::computeDagTransitiveClosure(dag, closure);

    // 3 reaches 0, 1, 2, 3
    EXPECT_TRUE(closure.test(3U, 0U));
    EXPECT_TRUE(closure.test(3U, 1U));
    EXPECT_TRUE(closure.test(3U, 2U));
    EXPECT_TRUE(closure.test(3U, 3U));

    // 2 reaches 0, 1, 2
    EXPECT_TRUE(closure.test(2U, 0U));
    EXPECT_TRUE(closure.test(2U, 1U));
    EXPECT_TRUE(closure.test(2U, 2U));
    EXPECT_FALSE(closure.test(2U, 3U));

    // 1 reaches 0, 1
    EXPECT_TRUE(closure.test(1U, 0U));
    EXPECT_TRUE(closure.test(1U, 1U));
    EXPECT_FALSE(closure.test(1U, 2U));

    // 0 reaches only 0
    EXPECT_TRUE(closure.test(0U, 0U));
    EXPECT_FALSE(closure.test(0U, 1U));
}

TEST(SccCompressedClosure, DagClosureBranchingAndDiamondsWithKahnSort) {
    // Diamond with scrambled vertex indices:
    // 5 -> 3, 5 -> 2
    // 3 -> 0, 2 -> 0
    // 4 -> 5 (predecessor of source)
    // 1 is isolated
    hbrick::CsrGraphBuilder builder{6U};
    builder.addEdge(4U, 5U);
    builder.addEdge(5U, 3U);
    builder.addEdge(5U, 2U);
    builder.addEdge(3U, 0U);
    builder.addEdge(2U, 0U);
    const hbrick::CsrGraph dag = builder.build();

    hbrick::BitMatrix closure(6U, 6U);
    for (uint32_t u = 0U; u < 6U; ++u) {
        closure.set(u, u);
        for (const uint32_t v : dag.outNeighbors(u)) {
            closure.set(u, v);
        }
    }

    hbrick::computeDagTransitiveClosure(dag, closure);

    // 4 reaches 5, 3, 2, 0, 4
    EXPECT_TRUE(closure.test(4U, 4U));
    EXPECT_TRUE(closure.test(4U, 5U));
    EXPECT_TRUE(closure.test(4U, 3U));
    EXPECT_TRUE(closure.test(4U, 2U));
    EXPECT_TRUE(closure.test(4U, 0U));
    EXPECT_FALSE(closure.test(4U, 1U));

    // 5 reaches 3, 2, 0, 5
    EXPECT_TRUE(closure.test(5U, 0U));
    EXPECT_FALSE(closure.test(5U, 4U));

    // 1 reaches only itself
    EXPECT_TRUE(closure.test(1U, 1U));
    EXPECT_FALSE(closure.test(1U, 0U));
    EXPECT_FALSE(closure.test(1U, 5U));
}

TEST(SccCompressedClosure, DagClosureEmptyAndSingleVertex) {
    // 0 vertices
    hbrick::CsrGraphBuilder empty_builder{0U};
    const hbrick::CsrGraph empty_dag = empty_builder.build();
    hbrick::BitMatrix empty_closure{};
    hbrick::computeDagTransitiveClosure(empty_dag, empty_closure);
    EXPECT_EQ(empty_closure.numRows(), 0U);

    // 1 vertex
    hbrick::CsrGraphBuilder single_builder{1U};
    const hbrick::CsrGraph single_dag = single_builder.build();
    hbrick::BitMatrix single_closure(1U, 1U);
    single_closure.set(0U, 0U);
    hbrick::computeDagTransitiveClosure(single_dag, single_closure);
    EXPECT_TRUE(single_closure.test(0U, 0U));
}

TEST(SccCompressedClosure, DagClosureDisconnectedComponents) {
    // 3 disconnected sub-DAGs:
    // Sub-DAG A: 0 -> 1 -> 2
    // Sub-DAG B: 3 -> 4
    // Sub-DAG C: 5 -> 6, 5 -> 7
    hbrick::CsrGraphBuilder builder{8U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 2U);
    builder.addEdge(3U, 4U);
    builder.addEdge(5U, 6U);
    builder.addEdge(5U, 7U);
    const hbrick::CsrGraph dag = builder.build();

    hbrick::BitMatrix closure(8U, 8U);
    for (uint32_t u = 0U; u < 8U; ++u) {
        closure.set(u, u);
        for (const uint32_t v : dag.outNeighbors(u)) {
            closure.set(u, v);
        }
    }

    hbrick::computeDagTransitiveClosure(dag, closure);

    // Intra-component reachability
    EXPECT_TRUE(closure.test(0U, 2U));
    EXPECT_TRUE(closure.test(3U, 4U));
    EXPECT_TRUE(closure.test(5U, 6U));
    EXPECT_TRUE(closure.test(5U, 7U));

    // Zero cross-component reachability
    EXPECT_FALSE(closure.test(0U, 3U));
    EXPECT_FALSE(closure.test(3U, 0U));
    EXPECT_FALSE(closure.test(4U, 5U));
    EXPECT_FALSE(closure.test(5U, 1U));
    EXPECT_FALSE(closure.test(6U, 7U));
}

TEST(SccCompressedClosure, CompressionThresholdExactBoundaries) {
    // For V <= 1: always false
    EXPECT_FALSE(hbrick::shouldUseSccCompressedClosure(0U, 0U));
    EXPECT_FALSE(hbrick::shouldUseSccCompressedClosure(1U, 1U));
    EXPECT_FALSE(hbrick::shouldUseSccCompressedClosure(1U, 0U));

    // For C >= V: always false
    EXPECT_FALSE(hbrick::shouldUseSccCompressedClosure(10U, 10U));
    EXPECT_FALSE(hbrick::shouldUseSccCompressedClosure(10U, 11U));

    // For small V <= 16: any reduction C < V is accepted
    EXPECT_TRUE(hbrick::shouldUseSccCompressedClosure(16U, 15U));
    EXPECT_TRUE(hbrick::shouldUseSccCompressedClosure(8U, 7U));

    // For V > 16: C / V <= 0.85
    // V = 20:
    // C = 17 -> 17/20 = 0.85 -> true
    EXPECT_TRUE(hbrick::shouldUseSccCompressedClosure(20U, 17U));
    // C = 18 -> 18/20 = 0.90 -> false
    EXPECT_FALSE(hbrick::shouldUseSccCompressedClosure(20U, 18U));

    // V = 100:
    // C = 85 -> true
    EXPECT_TRUE(hbrick::shouldUseSccCompressedClosure(100U, 85U));
    // C = 86 -> false
    EXPECT_FALSE(hbrick::shouldUseSccCompressedClosure(100U, 86U));
    // C = 1 (single giant component) -> true
    EXPECT_TRUE(hbrick::shouldUseSccCompressedClosure(100U, 1U));
}
