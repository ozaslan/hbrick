#include <gtest/gtest.h>

#include <string>

#include "maze_generator.hpp"
#include "reachability_oracle.hpp"

namespace {

struct MazeScenario {
    std::string name;
    hbrick::test_support::MazeParams params;
    uint64_t opening_seed = 0U;
    uint32_t extra_openings = 0U;
};

struct GraphModeScenario {
    std::string name;
    hbrick::GridEdgeConversionMode mode;
    hbrick::RandomAsymmetricParams random_params{};
};

[[nodiscard]] hbrick::PassableGrid buildMaze(const MazeScenario& scenario) {
    if (scenario.extra_openings == 0U) {
        return hbrick::test_support::generatePerfectMaze(scenario.params);
    }

    return hbrick::test_support::generateMazeWithExtraPassages(
        scenario.params,
        scenario.opening_seed,
        scenario.extra_openings
    );
}

void runOracleOnMaze(
    const MazeScenario& maze_scenario,
    const GraphModeScenario& mode_scenario
) {
    const hbrick::PassableGrid maze = buildMaze(maze_scenario);
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        maze,
        mode_scenario.mode,
        mode_scenario.random_params
    );

    const std::string context = maze_scenario.name + "/" + mode_scenario.name
        + " vertices=" + std::to_string(graph.numVertices());

    if (mode_scenario.mode == hbrick::GridEdgeConversionMode::BidirectionalAll) {
        // Bidirectional mazes collapse to one large SCC; closure preprocess is O(n^3).
        // Closure baselines are covered on cycle/acyclic graphs in other tests.
        hbrick::test_support::expectSearchBaselinesMatchBfs(graph, context);
        return;
    }

    hbrick::test_support::expectAllBaselinesMatchBfs(graph, context);
}

class MazeReachabilityOracleTest
    : public ::testing::TestWithParam<std::tuple<MazeScenario, GraphModeScenario>> {};

}  // namespace

TEST_P(MazeReachabilityOracleTest, AllBaselinesAgreeWithBfsOnAllPairs) {
    const MazeScenario& maze_scenario = std::get<0>(GetParam());
    const GraphModeScenario& mode_scenario = std::get<1>(GetParam());
    runOracleOnMaze(maze_scenario, mode_scenario);
}

INSTANTIATE_TEST_SUITE_P(
    PerfectMazesSmallAllModes,
    MazeReachabilityOracleTest,
    ::testing::Combine(
        ::testing::Values(
            MazeScenario{"perfect-12x10-seed-a", {12U, 10U, 0xA11CE5EEDULL}, 0U, 0U}
        ),
        ::testing::Values(
            GraphModeScenario{
                "acyclic-east-south",
                hbrick::GridEdgeConversionMode::AcyclicEastSouth
            },
            GraphModeScenario{
                "bidirectional-all",
                hbrick::GridEdgeConversionMode::BidirectionalAll
            },
            GraphModeScenario{
                "random-asymmetric",
                hbrick::GridEdgeConversionMode::RandomAsymmetric,
                hbrick::RandomAsymmetricParams{0x515EED01ULL, 0.14L, 0.26L}
            }
        )
    )
);

INSTANTIATE_TEST_SUITE_P(
    PerfectMazesLargeAcyclicAndRandom,
    MazeReachabilityOracleTest,
    ::testing::Combine(
        ::testing::Values(
            MazeScenario{"perfect-15x15-seed-b", {15U, 15U, 0xBADC0FFEULL}, 0U, 0U},
            MazeScenario{"perfect-18x14-seed-c", {18U, 14U, 0xC0FFEE01ULL}, 0U, 0U}
        ),
        ::testing::Values(
            GraphModeScenario{
                "acyclic-east-south",
                hbrick::GridEdgeConversionMode::AcyclicEastSouth
            },
            GraphModeScenario{
                "random-asymmetric",
                hbrick::GridEdgeConversionMode::RandomAsymmetric,
                hbrick::RandomAsymmetricParams{0x515EED04ULL, 0.14L, 0.26L}
            }
        )
    )
);

INSTANTIATE_TEST_SUITE_P(
    CyclicMazesSmallBidirectional,
    MazeReachabilityOracleTest,
    ::testing::Combine(
        ::testing::Values(
            MazeScenario{
                "cyclic-10x10-openings-bidirectional",
                {10U, 10U, 0xFACEFEEDULL},
                0x0A115A115ULL,
                28U
            }
        ),
        ::testing::Values(
            GraphModeScenario{
                "bidirectional-all",
                hbrick::GridEdgeConversionMode::BidirectionalAll
            }
        )
    )
);

INSTANTIATE_TEST_SUITE_P(
    CyclicMazesSmallRandom,
    MazeReachabilityOracleTest,
    ::testing::Combine(
        ::testing::Values(
            MazeScenario{
                "cyclic-12x12-openings-random",
                {12U, 12U, 0xFACEFEEDULL},
                0x0A115A115ULL,
                36U
            }
        ),
        ::testing::Values(
            GraphModeScenario{
                "random-asymmetric",
                hbrick::GridEdgeConversionMode::RandomAsymmetric,
                hbrick::RandomAsymmetricParams{0x515EED02ULL, 0.10L, 0.30L}
            }
        )
    )
);

INSTANTIATE_TEST_SUITE_P(
    CyclicMazesLargeRandom,
    MazeReachabilityOracleTest,
    ::testing::Combine(
        ::testing::Values(
            MazeScenario{
                "cyclic-18x14-openings",
                {18U, 14U, 0x123456789ABCDEF0ULL},
                0xBA115EEDULL,
                48U
            }
        ),
        ::testing::Values(
            GraphModeScenario{
                "random-asymmetric",
                hbrick::GridEdgeConversionMode::RandomAsymmetric,
                hbrick::RandomAsymmetricParams{0x515EED05ULL, 0.10L, 0.30L}
            }
        )
    )
);

TEST(MazeReachabilityOracle, ClosureBaselinesMatchBfsOnSmallBidirectionalMaze) {
    const hbrick::PassableGrid maze = hbrick::test_support::generatePerfectMaze(
        hbrick::test_support::MazeParams{8U, 8U, 0xB1D1EC011ULL}
    );
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        maze,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::test_support::expectAllBaselinesMatchBfs(
        graph,
        "perfect-8x8-bidirectional-closure vertices=" + std::to_string(graph.numVertices())
    );
}

TEST(MazeReachabilityOracle, AllBaselinesAgreeWithBfsOnLargeCyclicMaze) {
    runOracleOnMaze(
        MazeScenario{
            "large-cyclic-22x18-openings",
            {22U, 18U, 0xF00DF00DULL},
            0xBBAA11A1ULL,
            64U
        },
        GraphModeScenario{
            "random-asymmetric",
            hbrick::GridEdgeConversionMode::RandomAsymmetric,
            hbrick::RandomAsymmetricParams{0x515EED03ULL, 0.12L, 0.28L}
        }
    );
}
