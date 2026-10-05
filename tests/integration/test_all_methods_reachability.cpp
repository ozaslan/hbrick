#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "hbrick/baselines/csr_bfs_baseline.hpp"
#include "hbrick/baselines/csr_dfs_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/dfs.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"
#include "maze_generator.hpp"
#include "reachability_oracle.hpp"
#include "test_limits.hpp"

namespace {

hbrick::CsrGraph buildDiamondGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 2U);
    builder.addEdge(1U, 3U);
    builder.addEdge(2U, 3U);
    return builder.build();
}

hbrick::CsrGraph buildCycleWithExitGraph() {
    hbrick::CsrGraphBuilder builder{5U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 2U);
    builder.addEdge(2U, 0U);
    builder.addEdge(2U, 3U);
    builder.addEdge(3U, 4U);
    return builder.build();
}

hbrick::CsrGraph buildDisconnectedComponentsGraph() {
    hbrick::CsrGraphBuilder builder{6U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 2U);
    builder.addEdge(3U, 4U);
    builder.addEdge(4U, 5U);
    builder.addEdge(5U, 3U);
    return builder.build();
}

hbrick::CsrGraph buildChainDagGraph() {
    hbrick::CsrGraphBuilder builder{8U};
    for (uint32_t vertex = 0U; vertex + 1U < 8U; ++vertex) {
        builder.addEdge(vertex, vertex + 1U);
    }
    builder.addEdge(0U, 4U);
    builder.addEdge(2U, 7U);
    return builder.build();
}

hbrick::CsrGraph buildCompleteTournamentGraph(const uint32_t num_vertices) {
    hbrick::CsrGraphBuilder builder{num_vertices};
    for (uint32_t source = 0U; source < num_vertices; ++source) {
        for (uint32_t target = source + 1U; target < num_vertices; ++target) {
            builder.addEdge(source, target);
        }
    }
    return builder.build();
}

void expectBfsDfsPrimitivesAgree(const hbrick::CsrGraph& graph, const std::string& context) {
    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    hbrick::GraphSearchScratch dfs_scratch(graph.numVertices());

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer bfs_answer =
                hbrick::Bfs::reachable(graph, source, target, bfs_scratch);
            const hbrick::ReachabilityAnswer dfs_answer =
                hbrick::Dfs::reachable(graph, source, target, dfs_scratch);
            EXPECT_EQ(bfs_answer, dfs_answer)
                << context << " primitive disagreement source=" << source
                << " target=" << target;
        }
    }
}

void expectBaselineWrappersMatchPrimitives(
    const hbrick::CsrGraph& graph,
    const std::string& context
) {
    hbrick::CsrBfsBaseline csr_bfs;
    hbrick::CsrDfsBaseline csr_dfs;
    csr_bfs.preprocess(graph);
    csr_dfs.preprocess(graph);
    ASSERT_EQ(csr_bfs.status(), hbrick::BaselineStatus::Completed) << context;
    ASSERT_EQ(csr_dfs.status(), hbrick::BaselineStatus::Completed) << context;

    hbrick::GraphSearchScratch scratch(graph.numVertices());
    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer expected =
                hbrick::test_support::bfsReference(graph, source, target, scratch);
            EXPECT_EQ(csr_bfs.query(source, target, scratch), expected)
                << context << " CsrBfs source=" << source << " target=" << target;
            EXPECT_EQ(csr_dfs.query(source, target, scratch), expected)
                << context << " CsrDfs source=" << source << " target=" << target;
        }
    }
}

struct MazeModeCase {
    std::string name;
    hbrick::test_support::MazeParams params;
    hbrick::GridEdgeConversionMode mode;
    hbrick::RandomAsymmetricParams random_params{};
    uint64_t opening_seed = 0U;
    uint32_t extra_openings = 0U;
};

[[nodiscard]] hbrick::MazeLayout buildCaseMaze(const MazeModeCase& scenario) {
    if (scenario.extra_openings == 0U) {
        return hbrick::test_support::generatePerfectMaze(scenario.params);
    }
    return hbrick::test_support::generateMazeWithExtraPassages(
        scenario.params,
        scenario.opening_seed,
        scenario.extra_openings
    );
}

}  // namespace

TEST(AllMethodsReachability, HandCraftedGraphsMatchBfsOracle) {
    const std::vector<std::pair<std::string, hbrick::CsrGraph>> graphs{
        {"diamond", buildDiamondGraph()},
        {"cycle-with-exit", buildCycleWithExitGraph()},
        {"disconnected", buildDisconnectedComponentsGraph()},
        {"chain-dag", buildChainDagGraph()},
        {"tournament-7", buildCompleteTournamentGraph(7U)},
    };

    for (const auto& [name, graph] : graphs) {
        expectBfsDfsPrimitivesAgree(graph, name);
        expectBaselineWrappersMatchPrimitives(graph, name);
        hbrick::test_support::expectAllBaselinesMatchBfs(graph, name);
        hbrick::test_support::expectSccPartitionMatchesBidirectionalBfs(graph, name);
    }
}

TEST(AllMethodsReachability, ReflexivePairsAreReachableAcrossMethods) {
    const hbrick::CsrGraph graph = buildDisconnectedComponentsGraph();
    hbrick::test_support::expectAllBaselinesMatchBfs(graph, "reflexive-disconnected");

    hbrick::GraphSearchScratch scratch(graph.numVertices());
    for (uint32_t vertex = 0U; vertex < graph.numVertices(); ++vertex) {
        EXPECT_EQ(
            hbrick::test_support::bfsReference(graph, vertex, vertex, scratch),
            hbrick::ReachabilityAnswer::Reachable
        ) << "vertex=" << vertex;
    }
}

TEST(AllMethodsReachability, CrossComponentPairsAreUnreachable) {
    const hbrick::CsrGraph graph = buildDisconnectedComponentsGraph();
    hbrick::GraphSearchScratch scratch(graph.numVertices());

    EXPECT_EQ(
        hbrick::test_support::bfsReference(graph, 0U, 3U, scratch),
        hbrick::ReachabilityAnswer::Unreachable
    );
    EXPECT_EQ(
        hbrick::test_support::bfsReference(graph, 5U, 1U, scratch),
        hbrick::ReachabilityAnswer::Unreachable
    );

    hbrick::test_support::expectAllBaselinesMatchBfs(graph, "cross-component");
}

class AllMethodsMazeOracleTest : public ::testing::TestWithParam<MazeModeCase> {};

TEST_P(AllMethodsMazeOracleTest, AllImplementedMethodsMatchBfs) {
    const MazeModeCase& scenario = GetParam();
    const hbrick::MazeLayout maze = buildCaseMaze(scenario);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        maze,
        scenario.mode,
        scenario.random_params
    );

    const std::string context = scenario.name + " vertices="
        + std::to_string(graph.numVertices());
    const hbrick::HBrickConfig hbrick_config =
        hbrick::test_support::defaultOracleHBrickConfig();

    expectBfsDfsPrimitivesAgree(graph, context);
    hbrick::test_support::expectReachabilityOracleAllSlices(
        graph,
        context,
        scenario.mode,
        hbrick::test_support::kFullAllPairsVertexLimit,
        std::numeric_limits<uint64_t>::max(),
        &maze,
        hbrick::TileSize{4U, 4U},
        &hbrick_config
    );
}

INSTANTIATE_TEST_SUITE_P(
    DirectedGridFamilies,
    AllMethodsMazeOracleTest,
    ::testing::Values(
        MazeModeCase{
            "perfect-8x8-acyclic",
            {8U, 8U, 0xA11CE001ULL},
            hbrick::GridEdgeConversionMode::AcyclicEastSouth
        },
        MazeModeCase{
            "perfect-8x8-bidirectional",
            {8U, 8U, 0xA11CE002ULL},
            hbrick::GridEdgeConversionMode::BidirectionalAll
        },
        MazeModeCase{
            "perfect-10x8-random",
            {10U, 8U, 0xA11CE003ULL},
            hbrick::GridEdgeConversionMode::RandomAsymmetric,
            hbrick::RandomAsymmetricParams{0x515EED11ULL, 0.12L, 0.28L}
        },
        MazeModeCase{
            "perfect-10x8-gradient",
            {10U, 8U, 0xA11CE004ULL},
            hbrick::GridEdgeConversionMode::GradientFlow,
            hbrick::RandomAsymmetricParams{0x515EED12ULL, 0.12L, 0.28L, 40.0, 0.07L}
        },
        MazeModeCase{
            "cyclic-10x10-random",
            {10U, 10U, 0xC1C11C01ULL},
            hbrick::GridEdgeConversionMode::RandomAsymmetric,
            hbrick::RandomAsymmetricParams{0x515EED13ULL, 0.10L, 0.30L},
            0x0A11C1C1ULL,
            24U
        },
        MazeModeCase{
            "cyclic-12x10-bidirectional",
            {12U, 10U, 0xC1C11C02ULL},
            hbrick::GridEdgeConversionMode::BidirectionalAll,
            {},
            0x0A11C1C2ULL,
            30U
        }
    )
);

TEST(AllMethodsReachability, SampledOracleCoversIndexAndTileBaselines) {
    const hbrick::MazeLayout maze = hbrick::test_support::generatePerfectMaze(
        hbrick::test_support::MazeParams{12U, 10U, 0x5A101DULL}
    );
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        maze,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0x515EED20ULL, 0.14L, 0.26L}
    );

    const hbrick::HBrickConfig hbrick_config =
        hbrick::test_support::defaultOracleHBrickConfig();

    hbrick::test_support::expectReachabilityOracleSampledPairs(
        graph,
        "sampled-12x10-random vertices=" + std::to_string(graph.numVertices()),
        2500U,
        std::numeric_limits<uint64_t>::max(),
        &maze,
        hbrick::TileSize{4U, 4U},
        &hbrick_config
    );
}
