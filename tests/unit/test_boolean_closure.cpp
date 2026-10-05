#include <gtest/gtest.h>
#include <limits>
#include <random>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/bit/kleene_squaring_options.hpp"
#include "hbrick/graph/connected_components.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/kleene_squaring_bounds.hpp"
#include "hbrick/tile/tile_closure_util.hpp"

namespace {

void setReflexive(hbrick::BitMatrix& matrix) {
    const uint32_t n = matrix.numRows();
    for (uint32_t vertex = 0; vertex < n; ++vertex) {
        matrix.set(vertex, vertex);
    }
}

}  // namespace

TEST(BooleanClosure, BooleanMultiplyHandlesRectangularInnerDimension) {
    hbrick::BitMatrix lhs(2U, 4U);
    hbrick::BitMatrix rhs(4U, 3U);
    lhs.set(0U, 2U);
    rhs.set(2U, 1U);

    const hbrick::BitMatrix product = hbrick::booleanMultiply(lhs, rhs);

    ASSERT_EQ(product.numRows(), 2U);
    ASSERT_EQ(product.numCols(), 3U);
    EXPECT_TRUE(product.test(0U, 1U));
    EXPECT_FALSE(product.test(0U, 0U));
    EXPECT_FALSE(product.test(1U, 1U));
}

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
            hbrick::GraphSearchScratch scratch{num_vertices};
            const uint32_t squaring_count =
                hbrick::kleeneSquaringCountForCsrGraph(graph, scratch);

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

TEST(BooleanClosure, BooleanMultiplyIntoHandlesAliasedDestination) {
    hbrick::BitMatrix m(3U, 3U);
    setReflexive(m);
    m.set(0U, 1U);
    m.set(1U, 2U);

    // Multiply m * m into m directly (self-aliasing out with lhs and rhs)
    hbrick::booleanMultiplyInto(m, m, m);

    // Transitive edge (0, 2) should be set
    EXPECT_TRUE(m.test(0U, 2U));
    EXPECT_TRUE(m.test(0U, 1U));
    EXPECT_TRUE(m.test(1U, 2U));
    EXPECT_TRUE(m.test(0U, 0U));
    EXPECT_FALSE(m.test(2U, 0U));
}

TEST(BooleanClosure, KleeneSquaringInPlaceHandlesSelfScratchAliasing) {
    // 4-vertex chain 0 -> 1 -> 2 -> 3
    hbrick::BitMatrix relation(4U, 4U);
    setReflexive(relation);
    relation.set(0U, 1U);
    relation.set(1U, 2U);
    relation.set(2U, 3U);

    hbrick::BitMatrix expected = relation;
    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(expected);

    // Pass &relation as scratch (aliased scratch)
    hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(relation, 2U, &relation);

    EXPECT_TRUE(hbrick::bitMatricesEqual(relation, expected));
}

TEST(BooleanClosure, KleeneSquaringStepInPlaceHandlesSelfScratchAliasing) {
    hbrick::BitMatrix relation(4U, 4U);
    setReflexive(relation);
    relation.set(0U, 1U);
    relation.set(1U, 2U);
    relation.set(2U, 3U);

    // Step with scratch aliased to relation
    EXPECT_FALSE(hbrick::BooleanClosure::transitiveClosureKleeneSquaringStepInPlace(relation, relation));
    // 0 should now reach 2, 1 should reach 3
    EXPECT_TRUE(relation.test(0U, 2U));
    EXPECT_TRUE(relation.test(1U, 3U));
    EXPECT_TRUE(relation.test(0U, 1U));

    // Second step
    EXPECT_FALSE(hbrick::BooleanClosure::transitiveClosureKleeneSquaringStepInPlace(relation, relation));
    EXPECT_TRUE(relation.test(0U, 3U));
}

TEST(BooleanClosure, KleeneSquaringCountForLargestComponentHandlesEdgeCases) {
    using hbrick::BooleanClosure;
    EXPECT_EQ(BooleanClosure::kleeneSquaringCountForLargestComponent(0U), 0U);
    EXPECT_EQ(BooleanClosure::kleeneSquaringCountForLargestComponent(1U), 0U);
    EXPECT_EQ(BooleanClosure::kleeneSquaringCountForLargestComponent(2U), 1U);
    EXPECT_EQ(BooleanClosure::kleeneSquaringCountForLargestComponent(3U), 2U);
    EXPECT_EQ(BooleanClosure::kleeneSquaringCountForLargestComponent(4U), 2U);
    EXPECT_EQ(BooleanClosure::kleeneSquaringCountForLargestComponent(5U), 3U);
    EXPECT_EQ(BooleanClosure::kleeneSquaringCountForLargestComponent(1024U), 10U);
    EXPECT_EQ(BooleanClosure::kleeneSquaringCountForLargestComponent(1025U), 11U);
    EXPECT_EQ(BooleanClosure::kleeneSquaringCountForLargestComponent(0x80000000U), 31U);
    EXPECT_EQ(BooleanClosure::kleeneSquaringCountForLargestComponent(0x80000001U), 32U);
    EXPECT_EQ(BooleanClosure::kleeneSquaringCountForLargestComponent(std::numeric_limits<uint32_t>::max()), 32U);
}

TEST(BooleanClosure, BooleanMultiplyIntoRejectsIncompatibleInnerDimensions) {
    // 1x2 matrix and 1x1 matrix have mismatched inner dimensions (2 != 1)
    hbrick::BitMatrix lhs(1U, 2U);
    lhs.set(0U, 1U);
    hbrick::BitMatrix rhs(1U, 1U);
    rhs.set(0U, 0U);
    hbrick::BitMatrix out(1U, 1U);

    // Must safely return false without buffer overflow
    EXPECT_FALSE(hbrick::booleanMultiplyInto(lhs, rhs, out));
}

TEST(BooleanClosure, BooleanMultiplyIntoResizesMismatchedOutputDestination) {
    // 2x3 lhs and 3x4 rhs
    hbrick::BitMatrix lhs(2U, 3U);
    lhs.set(0U, 1U);
    lhs.set(1U, 2U);

    hbrick::BitMatrix rhs(3U, 4U);
    rhs.set(1U, 3U);
    rhs.set(2U, 0U);

    // out is initially 1x1 (mismatched)
    hbrick::BitMatrix out(1U, 1U);

    // Should automatically resize out to 2x4 and compute product correctly
    EXPECT_TRUE(hbrick::booleanMultiplyInto(lhs, rhs, out));
    EXPECT_EQ(out.numRows(), 2U);
    EXPECT_EQ(out.numCols(), 4U);

    // row 0: lhs(0, 1) & rhs(1, 3) -> out(0, 3)
    EXPECT_TRUE(out.test(0U, 3U));
    EXPECT_FALSE(out.test(0U, 0U));

    // row 1: lhs(1, 2) & rhs(2, 0) -> out(1, 0)
    EXPECT_TRUE(out.test(1U, 0U));
    EXPECT_FALSE(out.test(1U, 3U));
}

TEST(BooleanClosure, AllClosureAlgorithmsMatchWarshallOracleOnDiverseGraphs) {
    std::mt19937_64 rng(12345ULL);
    std::uniform_real_distribution<double> dist01(0.0, 1.0);

    for (const uint32_t n : {16U, 32U, 64U, 128U}) {
        for (const double density : {0.05, 0.20, 0.60}) {
            hbrick::BitMatrix base(n, n);
            for (uint32_t i = 0U; i < n; ++i) {
                base.set(i, i);
                for (uint32_t j = 0U; j < n; ++j) {
                    if (i != j && dist01(rng) < density) {
                        base.set(i, j);
                    }
                }
            }

            // 1. Oracle: Warshall
            hbrick::BitMatrix oracle = base;
            hbrick::BooleanClosure::transitiveClosureWarshallInPlace(oracle);

            // 2. Gustavson Squaring (legacy)
            hbrick::BitMatrix mat_gustavson = base;
            hbrick::KleeneSquaringOptions opt_gustavson;
            opt_gustavson.algorithm = hbrick::ClosureAlgorithm::GustavsonSquaring;
            hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                mat_gustavson, 10U, nullptr, opt_gustavson
            );
            EXPECT_TRUE(hbrick::bitMatricesEqual(oracle, mat_gustavson));

            // 3. M4RM 4-Table
            hbrick::BitMatrix mat_m4rm4 = base;
            hbrick::KleeneSquaringOptions opt_m4rm4;
            opt_m4rm4.algorithm = hbrick::ClosureAlgorithm::M4RM4Table;
            hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                mat_m4rm4, 10U, nullptr, opt_m4rm4
            );
            EXPECT_TRUE(hbrick::bitMatricesEqual(oracle, mat_m4rm4));

            // 4. M4RM 8-Table
            hbrick::BitMatrix mat_m4rm8 = base;
            hbrick::KleeneSquaringOptions opt_m4rm8;
            opt_m4rm8.algorithm = hbrick::ClosureAlgorithm::M4RM8Table;
            hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                mat_m4rm8, 10U, nullptr, opt_m4rm8
            );
            EXPECT_TRUE(hbrick::bitMatricesEqual(oracle, mat_m4rm8));

            // 5. Hybrid (with threshold below n -> triggers M4RM)
            hbrick::BitMatrix mat_hybrid_m4rm = base;
            hbrick::KleeneSquaringOptions opt_hybrid_m4rm;
            opt_hybrid_m4rm.algorithm = hbrick::ClosureAlgorithm::Hybrid;
            opt_hybrid_m4rm.hybrid_warshall_threshold = n / 2U;
            hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                mat_hybrid_m4rm, 10U, nullptr, opt_hybrid_m4rm
            );
            EXPECT_TRUE(hbrick::bitMatricesEqual(oracle, mat_hybrid_m4rm));

            // 6. Hybrid (with threshold above n -> triggers Warshall)
            hbrick::BitMatrix mat_hybrid_warshall = base;
            hbrick::KleeneSquaringOptions opt_hybrid_warshall;
            opt_hybrid_warshall.algorithm = hbrick::ClosureAlgorithm::Hybrid;
            opt_hybrid_warshall.hybrid_warshall_threshold = n * 2U;
            hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                mat_hybrid_warshall, 10U, nullptr, opt_hybrid_warshall
            );
            EXPECT_TRUE(hbrick::bitMatricesEqual(oracle, mat_hybrid_warshall));
        }
    }
}

TEST(BooleanClosure, ClosureAlgorithmEnvironmentVariableOverride) {
    hbrick::KleeneSquaringOptions default_opts;

    // Default without env is Hybrid
    unsetenv("HBRICK_CLOSURE_ALGORITHM");
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(default_opts, 256U), hbrick::ClosureAlgorithm::Warshall);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(default_opts, 1024U), hbrick::ClosureAlgorithm::M4RM4Table);

    // Override to Gustavson
    setenv("HBRICK_CLOSURE_ALGORITHM", "gustavson", 1);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(default_opts, 256U), hbrick::ClosureAlgorithm::GustavsonSquaring);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(default_opts, 1024U), hbrick::ClosureAlgorithm::GustavsonSquaring);

    // Override to Warshall
    setenv("HBRICK_CLOSURE_ALGORITHM", "warshall", 1);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(default_opts, 256U), hbrick::ClosureAlgorithm::Warshall);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(default_opts, 1024U), hbrick::ClosureAlgorithm::Warshall);

    // Override to M4RM 4-Table
    setenv("HBRICK_CLOSURE_ALGORITHM", "m4rm4", 1);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(default_opts, 256U), hbrick::ClosureAlgorithm::M4RM4Table);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(default_opts, 1024U), hbrick::ClosureAlgorithm::M4RM4Table);

    // Override to M4RM 8-Table
    setenv("HBRICK_CLOSURE_ALGORITHM", "m4rm8", 1);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(default_opts, 256U), hbrick::ClosureAlgorithm::M4RM8Table);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(default_opts, 1024U), hbrick::ClosureAlgorithm::M4RM8Table);

    // Cleanup env
    unsetenv("HBRICK_CLOSURE_ALGORITHM");
}

TEST(BooleanClosure, HybridThresholdBranching) {
    hbrick::KleeneSquaringOptions opts;
    opts.algorithm = hbrick::ClosureAlgorithm::Hybrid;
    opts.hybrid_warshall_threshold = 512U;

    // Below threshold -> Warshall
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(opts, 128U), hbrick::ClosureAlgorithm::Warshall);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(opts, 512U), hbrick::ClosureAlgorithm::Warshall);

    // Above threshold -> M4RM
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(opts, 513U), hbrick::ClosureAlgorithm::M4RM4Table);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(opts, 2048U), hbrick::ClosureAlgorithm::M4RM4Table);

    // Test with HBRICK_CLOSURE_THRESHOLD env var
    setenv("HBRICK_CLOSURE_THRESHOLD", "1000", 1);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(opts, 800U), hbrick::ClosureAlgorithm::Warshall);
    EXPECT_EQ(hbrick::resolveClosureAlgorithm(opts, 1200U), hbrick::ClosureAlgorithm::M4RM4Table);
    unsetenv("HBRICK_CLOSURE_THRESHOLD");
}

TEST(BooleanClosure, M4RMParallelTwoThreadDirectedChainMatchesWarshall) {
    // 1024-vertex directed chain 0 -> 1 -> 2 -> ... -> 1023
    // Exercises maximum diameter (1023) across multiple squaring steps
    // specifically targeting thread_count = 2 where 4-table and 8-table
    // parallel workers must distribute table generation correctly.
    const uint32_t n = 1024U;
    hbrick::BitMatrix chain(n, n);
    for (uint32_t i = 0U; i < n; ++i) {
        chain.set(i, i);
        if (i + 1U < n) {
            chain.set(i, i + 1U);
        }
    }

    hbrick::BitMatrix oracle = chain;
    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(oracle);

    // Verify upper triangular closure: i <= j => oracle.test(i, j) == true
    EXPECT_TRUE(oracle.test(0U, n - 1U));
    EXPECT_FALSE(oracle.test(n - 1U, 0U));

    // 1. M4RM 4-Table with thread_count = 2
    {
        hbrick::BitMatrix mat = chain;
        hbrick::KleeneSquaringOptions opts;
        opts.algorithm = hbrick::ClosureAlgorithm::M4RM4Table;
        opts.use_parallel = true;
        opts.num_threads = 2U;
        uint32_t executed_squarings = 0U;
        bool reached_fixpoint = false;
        hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            mat, 12U, nullptr, opts, &executed_squarings, &reached_fixpoint
        );
        EXPECT_TRUE(reached_fixpoint);
        EXPECT_GE(executed_squarings, 10U);
        EXPECT_TRUE(hbrick::bitMatricesEqual(oracle, mat));
    }

    // 2. M4RM 8-Table with thread_count = 2
    {
        hbrick::BitMatrix mat = chain;
        hbrick::KleeneSquaringOptions opts;
        opts.algorithm = hbrick::ClosureAlgorithm::M4RM8Table;
        opts.use_parallel = true;
        opts.num_threads = 2U;
        uint32_t executed_squarings = 0U;
        bool reached_fixpoint = false;
        hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            mat, 12U, nullptr, opts, &executed_squarings, &reached_fixpoint
        );
        EXPECT_TRUE(reached_fixpoint);
        EXPECT_GE(executed_squarings, 10U);
        EXPECT_TRUE(hbrick::bitMatricesEqual(oracle, mat));
    }
}

TEST(BooleanClosure, M4RMParallelScalingAcrossThreadCountsAndGraphTopologies) {
    // Test across various thread counts: 1, 2, 3, 4, 6, 8
    const std::vector<uint32_t> thread_counts = {1U, 2U, 3U, 4U, 6U, 8U};

    // Test sizes: exactly 1024, and unaligned non-power-of-two 1030
    for (const uint32_t n : {1024U, 1030U}) {
        // Directed cycle with chords
        hbrick::BitMatrix base(n, n);
        for (uint32_t i = 0U; i < n; ++i) {
            base.set(i, i);
            base.set(i, (i + 1U) % n);
            if (i % 7U == 0U) {
                base.set(i, (i + 101U) % n);
            }
        }

        hbrick::BitMatrix oracle = base;
        hbrick::BooleanClosure::transitiveClosureWarshallInPlace(oracle);

        for (const uint32_t threads : thread_counts) {
            // Test 4-Table
            {
                hbrick::BitMatrix mat = base;
                hbrick::KleeneSquaringOptions opts;
                opts.algorithm = hbrick::ClosureAlgorithm::M4RM4Table;
                opts.use_parallel = (threads > 1U);
                opts.num_threads = threads;
                hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                    mat, 12U, nullptr, opts
                );
                EXPECT_TRUE(hbrick::bitMatricesEqual(oracle, mat))
                    << "Failed on M4RM 4-Table, n=" << n << ", threads=" << threads;
            }

            // Test 8-Table
            {
                hbrick::BitMatrix mat = base;
                hbrick::KleeneSquaringOptions opts;
                opts.algorithm = hbrick::ClosureAlgorithm::M4RM8Table;
                opts.use_parallel = (threads > 1U);
                opts.num_threads = threads;
                hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                    mat, 12U, nullptr, opts
                );
                EXPECT_TRUE(hbrick::bitMatricesEqual(oracle, mat))
                    << "Failed on M4RM 8-Table, n=" << n << ", threads=" << threads;
            }

            // Test Hybrid
            {
                hbrick::BitMatrix mat = base;
                hbrick::KleeneSquaringOptions opts;
                opts.algorithm = hbrick::ClosureAlgorithm::Hybrid;
                opts.hybrid_warshall_threshold = 512U;
                opts.use_parallel = (threads > 1U);
                opts.num_threads = threads;
                hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                    mat, 12U, nullptr, opts
                );
                EXPECT_TRUE(hbrick::bitMatricesEqual(oracle, mat))
                    << "Failed on Hybrid, n=" << n << ", threads=" << threads;
            }
        }
    }
}

TEST(BooleanClosure, M4RMParallelRandomDenseAndSparseLargeGraphs) {
    std::mt19937_64 rng(424242ULL);
    std::uniform_real_distribution<double> dist01(0.0, 1.0);

    const uint32_t n = 1024U;
    for (const double density : {0.005, 0.05, 0.30}) {
        hbrick::BitMatrix base(n, n);
        for (uint32_t i = 0U; i < n; ++i) {
            base.set(i, i);
            for (uint32_t j = 0U; j < n; ++j) {
                if (i != j && dist01(rng) < density) {
                    base.set(i, j);
                }
            }
        }

        hbrick::BitMatrix oracle = base;
        hbrick::BooleanClosure::transitiveClosureWarshallInPlace(oracle);

        // Test with 2 threads and 5 threads
        for (const uint32_t threads : {2U, 5U}) {
            hbrick::BitMatrix mat4 = base;
            hbrick::KleeneSquaringOptions opt4;
            opt4.algorithm = hbrick::ClosureAlgorithm::M4RM4Table;
            opt4.use_parallel = true;
            opt4.num_threads = threads;
            hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                mat4, 10U, nullptr, opt4
            );
            EXPECT_TRUE(hbrick::bitMatricesEqual(oracle, mat4))
                << "Failed 4-table on density=" << density << ", threads=" << threads;

            hbrick::BitMatrix mat8 = base;
            hbrick::KleeneSquaringOptions opt8;
            opt8.algorithm = hbrick::ClosureAlgorithm::M4RM8Table;
            opt8.use_parallel = true;
            opt8.num_threads = threads;
            hbrick::BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                mat8, 10U, nullptr, opt8
            );
            EXPECT_TRUE(hbrick::bitMatricesEqual(oracle, mat8))
                << "Failed 8-table on density=" << density << ", threads=" << threads;
        }
    }
}

