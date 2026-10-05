#include <gtest/gtest.h>

#include <limits>
#include <random>

#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/kleene_squaring_bounds.hpp"
#include "hbrick/tile/tile_closure_util.hpp"

TEST(KleeneSquaringBounds, StructuralBoundTightensManySmallSccs) {
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

    hbrick::GraphSearchScratch scratch{graph.numVertices()};
    const hbrick::KleeneSquaringBoundStats stats =
        hbrick::kleeneSquaringBoundStatsForCsrGraph(graph, scratch);

    EXPECT_EQ(stats.largest_undirected_component, 20U);
    EXPECT_EQ(stats.num_strongly_connected_components, 10U);
    EXPECT_EQ(stats.largest_strongly_connected_component, 2U);
    EXPECT_EQ(
        stats.squaring_count,
        hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(12U)
    );
    EXPECT_LT(
        stats.squaring_count,
        hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(20U)
    );
}

TEST(KleeneSquaringBounds, SingleLargeSccMatchesUndirectedBound) {
    hbrick::CsrGraphBuilder builder{8U};
    for (uint32_t from = 0U; from < 8U; ++from) {
        for (uint32_t to = 0U; to < 8U; ++to) {
            if (from != to) {
                builder.addEdge(from, to);
            }
        }
    }
    const hbrick::CsrGraph graph = builder.build();

    hbrick::GraphSearchScratch scratch{graph.numVertices()};
    const hbrick::KleeneSquaringBoundStats stats =
        hbrick::kleeneSquaringBoundStatsForCsrGraph(graph, scratch);

    EXPECT_EQ(stats.num_strongly_connected_components, 1U);
    EXPECT_EQ(stats.largest_strongly_connected_component, 8U);
    EXPECT_EQ(
        stats.squaring_count,
        hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(
            stats.largest_undirected_component
        )
    );
}

TEST(KleeneSquaringBounds, TruncatedKleeneMatchesWarshallOnRandomGraphs) {
    std::mt19937_64 rng{0x5CCB0ULL};
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
            const hbrick::BitMatrix base = hbrick::buildTileReflexiveAdjacencyOrThrow(
                graph,
                std::numeric_limits<uint64_t>::max()
            );

            hbrick::GraphSearchScratch scratch{num_vertices};
            const uint32_t squaring_count =
                hbrick::kleeneSquaringCountForCsrGraph(graph, scratch);

            hbrick::BitMatrix warshall = base;
            hbrick::BitMatrix kleene = base;
            hbrick::BooleanClosure::transitiveClosureWarshallInPlace(warshall);
            hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                kleene,
                squaring_count
            );

            EXPECT_TRUE(hbrick::bitMatricesEqual(warshall, kleene))
                << "M=" << num_vertices << " density=" << density;
        }
    }
}

TEST(KleeneSquaringBounds, ReflexiveAdjacencyBoundMatchesCsrBound) {
    hbrick::CsrGraphBuilder builder{12U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 2U);
    builder.addEdge(3U, 4U);
    builder.addEdge(5U, 6U);
    const hbrick::CsrGraph graph = builder.build();
    const hbrick::BitMatrix adjacency = hbrick::buildTileReflexiveAdjacencyOrThrow(
        graph,
        std::numeric_limits<uint64_t>::max()
    );

    hbrick::GraphSearchScratch scratch{graph.numVertices()};
    const hbrick::KleeneSquaringBoundStats csr_stats =
        hbrick::kleeneSquaringBoundStatsForCsrGraph(graph, scratch);
    const hbrick::KleeneSquaringBoundStats matrix_stats =
        hbrick::kleeneSquaringBoundStatsForReflexiveAdjacency(adjacency, scratch);

    EXPECT_EQ(csr_stats.squaring_count, matrix_stats.squaring_count);
    EXPECT_EQ(
        csr_stats.num_strongly_connected_components,
        matrix_stats.num_strongly_connected_components
    );
}
