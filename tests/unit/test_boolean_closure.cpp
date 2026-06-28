#include <gtest/gtest.h>
#include <limits>
#include <random>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/bit/kleene_squaring_options.hpp"
#include "hbrick/graph/connected_components.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/tile/tile_closure_util.hpp"

namespace {

void setReflexive(hbrick::BitMatrix& matrix) {
    const uint32_t n = matrix.numRows();
    for (uint32_t vertex = 0; vertex < n; ++vertex) {
        matrix.set(vertex, vertex);
    }
}

}  // namespace

TEST(BooleanClosure, ComputesManualThreeVertexClosure) {
    hbrick::BitMatrix relation(3U, 3U);
    setReflexive(relation);
    relation.set(0U, 1U);
    relation.set(1U, 2U);

    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(relation);

    EXPECT_TRUE(relation.test(0U, 2U));
    EXPECT_FALSE(relation.test(2U, 0U));
    EXPECT_TRUE(relation.test(1U, 2U));
}

TEST(BooleanClosure, CopyingOverloadMatchesInPlaceResult) {
    hbrick::BitMatrix in_place(4U, 4U);
    hbrick::BitMatrix copied(4U, 4U);
    setReflexive(in_place);
    setReflexive(copied);
    in_place.set(0U, 1U);
    in_place.set(1U, 2U);
    in_place.set(2U, 3U);
    copied.set(0U, 1U);
    copied.set(1U, 2U);
    copied.set(2U, 3U);

    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(in_place);
    const hbrick::BitMatrix closed = hbrick::BooleanClosure::transitiveClosureWarshall(copied);

    for (uint32_t row = 0; row < 4U; ++row) {
        for (uint32_t col = 0; col < 4U; ++col) {
            EXPECT_EQ(in_place.test(row, col), closed.test(row, col)) << row << "," << col;
        }
    }
}

TEST(BooleanClosure, NonSquareMatrixIsLeftUnchanged) {
    hbrick::BitMatrix relation(3U, 5U);
    relation.set(0U, 1U);

    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(relation);

    EXPECT_TRUE(relation.test(0U, 1U));
    EXPECT_FALSE(relation.test(0U, 4U));
}

TEST(BooleanClosure, ZeroSizeMatrixIsNoOp) {
    hbrick::BitMatrix relation(0U, 0U);

    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(relation);
    const hbrick::BitMatrix closed = hbrick::BooleanClosure::transitiveClosureWarshall(relation);

    EXPECT_EQ(closed.numRows(), 0U);
    EXPECT_EQ(closed.numCols(), 0U);
}

TEST(BooleanClosure, WorksWithNonMultipleOf64Columns) {
    hbrick::BitMatrix relation(70U, 70U);
    setReflexive(relation);
    relation.set(0U, 69U);
    relation.set(69U, 68U);

    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(relation);

    EXPECT_TRUE(relation.test(0U, 68U));
    EXPECT_FALSE(relation.test(68U, 0U));
}

TEST(BooleanClosure, KleeneSquaringCountMatchesCeilLog2) {
    EXPECT_EQ(hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(0U), 0U);
    EXPECT_EQ(hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(1U), 0U);
    EXPECT_EQ(hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(2U), 1U);
    EXPECT_EQ(hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(3U), 2U);
    EXPECT_EQ(hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(4U), 2U);
    EXPECT_EQ(hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(8U), 3U);
    EXPECT_EQ(hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(9U), 4U);
}

TEST(BooleanClosure, KleeneSquaringMatchesWarshallOnManualGraph) {
    hbrick::BitMatrix warshall(5U, 5U);
    hbrick::BitMatrix kleene(5U, 5U);
    setReflexive(warshall);
    setReflexive(kleene);
    warshall.set(0U, 1U);
    warshall.set(1U, 2U);
    warshall.set(2U, 3U);
    warshall.set(3U, 4U);
    kleene.set(0U, 1U);
    kleene.set(1U, 2U);
    kleene.set(2U, 3U);
    kleene.set(3U, 4U);

    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(warshall);
    hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
        kleene,
        hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(5U)
    );

    EXPECT_TRUE(hbrick::bitMatricesEqual(warshall, kleene));
}

TEST(BooleanClosure, KleeneSquaringUsesLargestComponentForDisconnectedGraph) {
    hbrick::BitMatrix warshall(6U, 6U);
    hbrick::BitMatrix kleene(6U, 6U);
    setReflexive(warshall);
    setReflexive(kleene);

    // Component A: vertices 0-1
    warshall.set(0U, 1U);
    kleene.set(0U, 1U);

    // Component B: chain 2-3-4-5 (size 4)
    warshall.set(2U, 3U);
    warshall.set(3U, 4U);
    warshall.set(4U, 5U);
    kleene.set(2U, 3U);
    kleene.set(3U, 4U);
    kleene.set(4U, 5U);

    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(warshall);
    hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
        kleene,
        hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(4U)
    );

    EXPECT_TRUE(hbrick::bitMatricesEqual(warshall, kleene));
    EXPECT_FALSE(warshall.test(0U, 2U));
    EXPECT_TRUE(warshall.test(2U, 5U));
}

TEST(BooleanClosure, KleeneSquaringMatchesWarshallOnRandomGraphs) {
    std::mt19937_64 rng{0xB1C5ULL};
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    const uint32_t sizes[] = {4U, 8U, 16U, 32U, 64U, 70U};
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
            const uint32_t largest_component =
                hbrick::largestUndirectedComponentSize(graph);
            const uint32_t squaring_count =
                hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(
                    largest_component
                );

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

TEST(BooleanClosure, ParallelKleeneSquaringMatchesSerial) {
    std::mt19937_64 rng{0xC1E3EULL};
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    const uint32_t sizes[] = {64U, 96U, 128U};
    const double densities[] = {0.08, 0.20};

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
            const uint32_t squaring_count =
                hbrick::BooleanClosure::kleeneSquaringCountForLargestComponent(
                    hbrick::largestUndirectedComponentSize(graph)
                );

            hbrick::BitMatrix serial = base;
            hbrick::BitMatrix parallel = base;
            hbrick::KleeneSquaringOptions parallel_options{};
            parallel_options.use_parallel = true;
            parallel_options.num_threads = 4U;

            hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                serial,
                squaring_count
            );
            hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                parallel,
                squaring_count,
                nullptr,
                parallel_options
            );

            EXPECT_TRUE(hbrick::bitMatricesEqual(serial, parallel))
                << "M=" << num_vertices << " density=" << density;
        }
    }
}
