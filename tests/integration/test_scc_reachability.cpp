#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "hbrick/baselines/scc_dag_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_search_baseline.hpp"
#include "hbrick/graph/condensation_graph.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/graph/scc_decomposition.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "maze_generator.hpp"
#include "reachability_oracle.hpp"
#include "test_limits.hpp"

namespace {

hbrick::CsrGraph buildCycleGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 2U);
    builder.addEdge(2U, 0U);
    builder.addEdge(2U, 3U);
    return builder.build();
}

hbrick::CsrGraph buildDiamondGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 2U);
    builder.addEdge(1U, 3U);
    builder.addEdge(2U, 3U);
    return builder.build();
}

struct GraphScenario {
    std::string name;
    hbrick::CsrGraph graph;
};

struct MazeGraphScenario {
    std::string name;
    hbrick::MazeLayout layout;
    hbrick::GridEdgeConversionMode mode = hbrick::GridEdgeConversionMode::RandomAsymmetric;
    hbrick::RandomAsymmetricParams params{};
};

[[nodiscard]] hbrick::CsrGraph graphFromMazeScenario(const MazeGraphScenario& scenario) {
    return hbrick::DirectedGridGraphBuilder::build(
        scenario.layout,
        scenario.mode,
        scenario.params
    ).csrGraph();
}

void expectSccReachabilityVerified(
    const hbrick::CsrGraph& graph,
    const std::string& context,
    const hbrick::GridEdgeConversionMode mode
) {
    hbrick::test_support::expectReachabilityOracleAllSlices(graph, context, mode);
}

class SccReachabilityOracleTest : public ::testing::TestWithParam<MazeGraphScenario> {};

}  // namespace

TEST(SccReachabilityOracle, BidirectionalBfsMatchesComponentLabelsOnCycleGraph) {
    const hbrick::CsrGraph graph = buildCycleGraph();
    hbrick::GraphSearchScratch scratch(graph.numVertices());

    const hbrick::SccDecomposition decomposition = hbrick::SccDecomposition::compute(graph, scratch);

    EXPECT_TRUE(hbrick::test_support::mutuallyReachableViaBidirectionalBfs(
        graph,
        0U,
        2U,
        scratch,
        scratch
    ));
    EXPECT_FALSE(hbrick::test_support::mutuallyReachableViaBidirectionalBfs(
        graph,
        0U,
        3U,
        scratch,
        scratch
    ));
    EXPECT_EQ(decomposition.componentOf(0U), decomposition.componentOf(2U));
    EXPECT_NE(decomposition.componentOf(2U), decomposition.componentOf(3U));

    hbrick::test_support::expectSccPartitionMatchesBidirectionalBfs(graph, "cycle-graph");
}

TEST(SccReachabilityOracle, BidirectionalBfsSeparatesAcyclicDiamondVertices) {
    const hbrick::CsrGraph graph = buildDiamondGraph();

    hbrick::GraphSearchScratch scratch(graph.numVertices());
    EXPECT_FALSE(hbrick::test_support::mutuallyReachableViaBidirectionalBfs(
        graph,
        0U,
        3U,
        scratch,
        scratch
    ));
    EXPECT_TRUE(hbrick::test_support::mutuallyReachableViaBidirectionalBfs(
        graph,
        1U,
        1U,
        scratch,
        scratch
    ));

    hbrick::test_support::expectSccPartitionMatchesBidirectionalBfs(graph, "diamond-dag");
}

TEST(SccReachabilityOracle, SccDagBaselinesAgreeWithForwardBfsAndPartition) {
    const std::vector<GraphScenario> scenarios{
        {"cycle", buildCycleGraph()},
        {"diamond-dag", buildDiamondGraph()},
    };

    for (const GraphScenario& scenario : scenarios) {
        expectSccReachabilityVerified(
            scenario.graph,
            scenario.name + " vertices=" + std::to_string(scenario.graph.numVertices()),
            hbrick::GridEdgeConversionMode::RandomAsymmetric
        );
    }
}

TEST(SccReachabilityOracle, SccDagBaselinesUseTwoBfsChecksForSameComponentPairs) {
    const hbrick::CsrGraph graph = buildCycleGraph();
    hbrick::GraphSearchScratch preprocess_scratch(graph.numVertices());
    hbrick::GraphSearchScratch query_scratch(graph.numVertices());
    hbrick::GraphSearchScratch left_to_right_scratch(graph.numVertices());
    hbrick::GraphSearchScratch right_to_left_scratch(graph.numVertices());

    hbrick::SccDagSearchBaseline search_baseline;
    search_baseline.preprocess(graph, preprocess_scratch);
    ASSERT_EQ(search_baseline.status(), hbrick::BaselineStatus::Completed);

    hbrick::SccDagClosureBaseline closure_baseline;
    closure_baseline.preprocess(
        graph,
        preprocess_scratch,
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(closure_baseline.status(), hbrick::BaselineStatus::Completed);

    const hbrick::SccDecomposition decomposition =
        hbrick::SccDecomposition::compute(graph, preprocess_scratch);

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer forward = hbrick::test_support::bfsReference(
                graph,
                source,
                target,
                query_scratch
            );
            const hbrick::ReachabilityAnswer search_answer = search_baseline.query(
                source,
                target,
                query_scratch
            );
            const hbrick::ReachabilityAnswer closure_answer = closure_baseline.query(
                source,
                target
            );

            EXPECT_EQ(search_answer, forward)
                << "search source=" << source << " target=" << target;
            EXPECT_EQ(closure_answer, forward)
                << "closure source=" << source << " target=" << target;

            if (decomposition.componentOf(source) == decomposition.componentOf(target)) {
                EXPECT_TRUE(hbrick::test_support::mutuallyReachableViaBidirectionalBfs(
                    graph,
                    source,
                    target,
                    left_to_right_scratch,
                    right_to_left_scratch
                )) << "same-component source=" << source << " target=" << target;
            }
        }
    }
}

TEST_P(SccReachabilityOracleTest, MazeDerivedGraphsPassBidirectionalSccOracleOnAllSlices) {
    const MazeGraphScenario& scenario = GetParam();
    const hbrick::CsrGraph graph = graphFromMazeScenario(scenario);

    const std::string context = scenario.name + " vertices=" + std::to_string(graph.numVertices());

    expectSccReachabilityVerified(graph, context, scenario.mode);
}

INSTANTIATE_TEST_SUITE_P(
    SyntheticMazesAllModes,
    SccReachabilityOracleTest,
    ::testing::Values(
        MazeGraphScenario{
            "perfect-12x10-acyclic",
            hbrick::test_support::generatePerfectMaze({12U, 10U, 0xA11CE5EEDULL}),
            hbrick::GridEdgeConversionMode::AcyclicEastSouth
        },
        MazeGraphScenario{
            "perfect-12x10-random",
            hbrick::test_support::generatePerfectMaze({12U, 10U, 0xA11CE5EEDULL}),
            hbrick::GridEdgeConversionMode::RandomAsymmetric,
            hbrick::RandomAsymmetricParams{0x515EED01ULL, 0.14L, 0.26L}
        },
        MazeGraphScenario{
            "perfect-12x10-gradient",
            hbrick::test_support::generatePerfectMaze({12U, 10U, 0xA11CE5EEDULL}),
            hbrick::GridEdgeConversionMode::GradientFlow,
            hbrick::RandomAsymmetricParams{
                0x515EED06ULL,
                0.14L,
                0.26L,
                45.0,
                0.08L
            }
        },
        MazeGraphScenario{
            "cyclic-12x12-random",
            hbrick::test_support::generateMazeWithExtraPassages(
                {12U, 12U, 0xFACEFEEDULL},
                0x0A115A115ULL,
                36U
            ),
            hbrick::GridEdgeConversionMode::RandomAsymmetric,
            hbrick::RandomAsymmetricParams{0x515EED02ULL, 0.10L, 0.30L}
        },
        MazeGraphScenario{
            "cyclic-18x14-gradient",
            hbrick::test_support::generateMazeWithExtraPassages(
                {18U, 14U, 0x123456789ABCDEF0ULL},
                0xBA115EEDULL,
                48U
            ),
            hbrick::GridEdgeConversionMode::GradientFlow,
            hbrick::RandomAsymmetricParams{
                0x515EED07ULL,
                0.12L,
                0.28L,
                30.0,
                0.10L
            }
        }
    )
);

TEST(SccReachabilityOracle, CondensationGraphPreservesReachabilityStructure) {
    const hbrick::CsrGraph graph = buildCycleGraph();
    hbrick::GraphSearchScratch scratch(graph.numVertices());

    const hbrick::SccDecomposition decomposition = hbrick::SccDecomposition::compute(graph, scratch);
    const hbrick::CondensationGraph condensation =
        hbrick::CondensationGraph::fromGraph(graph, decomposition);

    EXPECT_EQ(condensation.dag().numVertices(), decomposition.numComponents());
    hbrick::test_support::expectSccPartitionMatchesBidirectionalBfs(
        graph,
        "condensation-cycle-graph"
    );
}
