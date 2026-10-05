#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/edge32.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace {

std::vector<hbrick::Edge32> buildAcyclicEdges(const hbrick::MazeLayout& grid) {
    return hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::AcyclicEastSouth
    ).edges();
}

std::vector<hbrick::Edge32> buildBidirectionalEdges(const hbrick::MazeLayout& grid) {
    return hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    ).edges();
}

std::vector<hbrick::Edge32> buildRandomEdges(
    const hbrick::MazeLayout& grid,
    const hbrick::RandomAsymmetricParams& params
) {
    return hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        params
    ).edges();
}

bool edgeLess(const hbrick::Edge32 lhs, const hbrick::Edge32 rhs) {
    return lhs.from != rhs.from ? lhs.from < rhs.from : lhs.to < rhs.to;
}

}  // namespace

TEST(DirectedGridGraph, OutNeighborsMatchesCsrStorage) {
    const hbrick::MazeLayout grid(3U, 2U);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::AcyclicEastSouth
    );

    EXPECT_EQ(graph.width(), 3U);
    EXPECT_EQ(graph.height(), 2U);
    EXPECT_EQ(graph.numVertices(), 6U);
    EXPECT_EQ(graph.coordFromVertex(4U), hbrick::GridCoord(1U, 1U));
    EXPECT_EQ(graph.vertexFromCoord(hbrick::GridCoord{2U, 0U}), 2U);

    const std::span<const uint32_t> neighbors = graph.outNeighbors(0U);
    ASSERT_EQ(neighbors.size(), 2U);
    EXPECT_EQ(neighbors[0U], 1U);
    EXPECT_EQ(neighbors[1U], 3U);
}

TEST(DirectedGridGraph, AcyclicEastSouthIsDeterministicAndRowMajor) {
    const hbrick::MazeLayout grid(3U, 2U);
    const std::vector<hbrick::Edge32> first = buildAcyclicEdges(grid);
    const std::vector<hbrick::Edge32> second = buildAcyclicEdges(grid);

    ASSERT_EQ(first.size(), 7U);
    EXPECT_EQ(first, second);

    EXPECT_EQ(first[0U], hbrick::Edge32(0U, 1U));
    EXPECT_EQ(first[1U], hbrick::Edge32(0U, 3U));
    EXPECT_EQ(first[2U], hbrick::Edge32(1U, 2U));
    EXPECT_EQ(first[3U], hbrick::Edge32(1U, 4U));
    EXPECT_EQ(first[4U], hbrick::Edge32(2U, 5U));
    EXPECT_EQ(first[5U], hbrick::Edge32(3U, 4U));
    EXPECT_EQ(first[6U], hbrick::Edge32(4U, 5U));
}

TEST(DirectedGridGraph, BidirectionalModeDoesNotUseRng) {
    const hbrick::MazeLayout grid(2U, 2U);
    const std::vector<hbrick::Edge32> first = buildBidirectionalEdges(grid);
    const std::vector<hbrick::Edge32> second = buildBidirectionalEdges(grid);

    ASSERT_EQ(first.size(), 8U);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first[0U], hbrick::Edge32(0U, 1U));
    EXPECT_EQ(first[1U], hbrick::Edge32(0U, 2U));
    EXPECT_EQ(first[2U], hbrick::Edge32(1U, 0U));
    EXPECT_EQ(first[3U], hbrick::Edge32(1U, 3U));
    EXPECT_EQ(first[4U], hbrick::Edge32(2U, 0U));
    EXPECT_EQ(first[5U], hbrick::Edge32(2U, 3U));
    EXPECT_EQ(first[6U], hbrick::Edge32(3U, 1U));
    EXPECT_EQ(first[7U], hbrick::Edge32(3U, 2U));
}

TEST(DirectedGridGraph, SameSeedProducesIdenticalRandomGraph) {
    const hbrick::MazeLayout grid(4U, 3U);
    const hbrick::RandomAsymmetricParams params{
        0x123456789ABCDEF0ULL,
        0.25L,
        0.25L,
    };

    const std::vector<hbrick::Edge32> first = buildRandomEdges(grid, params);
    const std::vector<hbrick::Edge32> second = buildRandomEdges(grid, params);

    EXPECT_EQ(first, second);
    EXPECT_FALSE(first.empty());
}

TEST(DirectedGridGraph, DifferentSeedUsuallyProducesDifferentGraph) {
    const hbrick::MazeLayout grid(5U, 4U);
    const hbrick::RandomAsymmetricParams first_params{1ULL, 0.2L, 0.3L};
    const hbrick::RandomAsymmetricParams second_params{2ULL, 0.2L, 0.3L};

    const std::vector<hbrick::Edge32> first = buildRandomEdges(grid, first_params);
    const std::vector<hbrick::Edge32> second = buildRandomEdges(grid, second_params);

    EXPECT_NE(first, second);
}

TEST(DirectedGridGraph, RandomProcedureUsesIntegerThresholdsDocumentedInTest) {
    const hbrick::MazeLayout grid(2U, 1U);
    const hbrick::RandomAsymmetricParams completed =
        hbrick::completeRandomAsymmetricOrientation(
            hbrick::RandomAsymmetricParams{99ULL, 0.35L, 0.10L}
        );
    EXPECT_DOUBLE_EQ(static_cast<double>(completed.p_bidirectional), 0.35);
    EXPECT_DOUBLE_EQ(static_cast<double>(completed.p_one_way), 0.65);
    EXPECT_TRUE(hbrick::randomAsymmetricOrientationIsComplete(completed));
    EXPECT_FALSE(hbrick::randomAsymmetricOrientationIsComplete(
        hbrick::RandomAsymmetricParams{99ULL, 0.35L, 0.10L}
    ));

    const hbrick::RandomAsymmetricParams from_one_way =
        hbrick::completeRandomAsymmetricOrientation(
            hbrick::RandomAsymmetricParams{99ULL, 0.0L, 0.05L}
        );
    EXPECT_DOUBLE_EQ(static_cast<double>(from_one_way.p_one_way), 0.05);
    EXPECT_DOUBLE_EQ(static_cast<double>(from_one_way.p_bidirectional), 0.95);
    EXPECT_DOUBLE_EQ(static_cast<double>(from_one_way.p_edge_drop), 0.0);
    EXPECT_TRUE(hbrick::randomAsymmetricOrientationIsComplete(from_one_way));

    const hbrick::RandomAsymmetricParams always_bidirectional{99ULL, 1.0L, 0.0L};
    const hbrick::RandomAsymmetricParams always_one_way{99ULL, 0.0L, 1.0L};
    const hbrick::RandomAsymmetricParams never_add{99ULL, 0.0L, 0.0L};

    const std::vector<hbrick::Edge32> bidirectional = buildRandomEdges(grid, always_bidirectional);
    const std::vector<hbrick::Edge32> one_way = buildRandomEdges(grid, always_one_way);
    const std::vector<hbrick::Edge32> none = buildRandomEdges(grid, never_add);

    EXPECT_EQ(bidirectional, (std::vector<hbrick::Edge32>{hbrick::Edge32(0U, 1U), hbrick::Edge32(1U, 0U)}));
    EXPECT_EQ(one_way.size(), 1U);
    EXPECT_TRUE(one_way[0U] == hbrick::Edge32(0U, 1U) || one_way[0U] == hbrick::Edge32(1U, 0U));
    EXPECT_TRUE(none.empty());
}

TEST(DirectedGridGraph, GradientFlowWithoutNoiseMatchesAcyclicEastSouth) {
    const hbrick::MazeLayout grid(4U, 3U);

    hbrick::RandomAsymmetricParams params;
    params.seed = 7ULL;
    params.gradient_angle_degrees = 45.0;  // both east and south point downhill
    params.p_bidirectional = 0.0L;
    params.p_against_gradient = 0.0L;

    std::vector<hbrick::Edge32> gradient = hbrick::DirectedGridGraphBuilder::build(
        grid, hbrick::GridEdgeConversionMode::GradientFlow, params
    ).edges();
    std::vector<hbrick::Edge32> acyclic = buildAcyclicEdges(grid);

    std::sort(gradient.begin(), gradient.end(), edgeLess);
    std::sort(acyclic.begin(), acyclic.end(), edgeLess);
    EXPECT_EQ(gradient, acyclic);
}

TEST(DirectedGridGraph, GradientFlowReversedAngleReversesEveryArc) {
    const hbrick::MazeLayout grid(3U, 3U);

    hbrick::RandomAsymmetricParams params;
    params.seed = 11ULL;
    params.gradient_angle_degrees = 225.0;  // both axes point uphill
    params.p_bidirectional = 0.0L;
    params.p_against_gradient = 0.0L;

    std::vector<hbrick::Edge32> reversed = hbrick::DirectedGridGraphBuilder::build(
        grid, hbrick::GridEdgeConversionMode::GradientFlow, params
    ).edges();

    std::vector<hbrick::Edge32> expected;
    for (const hbrick::Edge32& edge : buildAcyclicEdges(grid)) {
        expected.emplace_back(edge.to, edge.from);
    }

    std::sort(reversed.begin(), reversed.end(), edgeLess);
    std::sort(expected.begin(), expected.end(), edgeLess);
    EXPECT_EQ(reversed, expected);
}

TEST(DirectedGridGraph, GradientFlowCoversEveryAdjacencyExactlyOnce) {
    hbrick::MazeLayout grid(5U, 4U);
    grid.setPassable(hbrick::GridCoord{2U, 1U}, false);

    uint64_t adjacencies = 0;
    grid.forEachPassableAdjacentPairEastSouth(
        [&adjacencies](const hbrick::GridCoord, const hbrick::GridCoord, const hbrick::Direction) {
            ++adjacencies;
        }
    );

    hbrick::RandomAsymmetricParams one_way;
    one_way.seed = 3ULL;
    one_way.gradient_angle_degrees = 120.0;
    one_way.p_bidirectional = 0.0L;
    one_way.p_against_gradient = 0.10L;

    hbrick::RandomAsymmetricParams all_bidirectional = one_way;
    all_bidirectional.p_bidirectional = 1.0L;

    const hbrick::DirectedGridGraph single = hbrick::DirectedGridGraphBuilder::build(
        grid, hbrick::GridEdgeConversionMode::GradientFlow, one_way
    );
    const hbrick::DirectedGridGraph doubled = hbrick::DirectedGridGraphBuilder::build(
        grid, hbrick::GridEdgeConversionMode::GradientFlow, all_bidirectional
    );

    EXPECT_EQ(single.numEdges(), adjacencies);
    EXPECT_EQ(doubled.numEdges(), 2U * adjacencies);
}

TEST(DirectedGridGraph, GradientFlowIsDeterministicPerSeed) {
    const hbrick::MazeLayout grid(6U, 5U);

    hbrick::RandomAsymmetricParams params;
    params.seed = 0xFEEDFACEULL;
    params.gradient_angle_degrees = 90.0;
    params.p_bidirectional = 0.15L;
    params.p_against_gradient = 0.05L;

    const auto buildEdges = [&grid, &params]() {
        return hbrick::DirectedGridGraphBuilder::build(
            grid, hbrick::GridEdgeConversionMode::GradientFlow, params
        ).edges();
    };

    EXPECT_EQ(buildEdges(), buildEdges());

    hbrick::RandomAsymmetricParams other = params;
    other.seed = params.seed + 1U;
    const std::vector<hbrick::Edge32> different =
        hbrick::DirectedGridGraphBuilder::build(
            grid, hbrick::GridEdgeConversionMode::GradientFlow, other
        ).edges();
    EXPECT_NE(buildEdges(), different);
}

TEST(DirectedGridGraph, SanitizesInvalidRandomAsymmetricProbabilities) {
    const hbrick::MazeLayout grid(4U, 3U);

    hbrick::RandomAsymmetricParams params;
    params.seed = 7ULL;
    params.p_bidirectional = std::numeric_limits<long double>::quiet_NaN();
    params.p_one_way = 2.5L;

    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        params
    );

    EXPECT_GT(graph.numEdges(), 0U);
}

TEST(DirectedGridGraph, ImpassableCellsRemoveIncidentPairs) {
    hbrick::MazeLayout grid(3U, 2U);
    grid.setPassable(hbrick::GridCoord{1U, 0U}, false);

    const std::vector<hbrick::Edge32> edges = buildAcyclicEdges(grid);

    ASSERT_EQ(edges.size(), 4U);
    EXPECT_EQ(edges[0U], hbrick::Edge32(0U, 3U));
    EXPECT_EQ(edges[1U], hbrick::Edge32(2U, 5U));
    EXPECT_EQ(edges[2U], hbrick::Edge32(3U, 4U));
    EXPECT_EQ(edges[3U], hbrick::Edge32(4U, 5U));
}

TEST(DirectedGridGraph, EnforceCompleteOrientationFailsWhenSumIsNotOne) {
    hbrick::RandomAsymmetricParams incomplete{1ULL, 0.9L, 0.9L};
    std::string error;
    EXPECT_FALSE(hbrick::enforceCompleteRandomAsymmetricOrientation(incomplete, error));
    EXPECT_FALSE(error.empty());

    hbrick::RandomAsymmetricParams complete{1ULL, 0.4L, 0.6L};
    error.clear();
    EXPECT_TRUE(hbrick::enforceCompleteRandomAsymmetricOrientation(complete, error));
    EXPECT_TRUE(hbrick::randomAsymmetricOrientationIsComplete(complete));
    EXPECT_DOUBLE_EQ(static_cast<double>(complete.p_one_way), 0.6);
}

TEST(DirectedGridGraph, FivePercentOneWayDoesNotDropAdjacencies) {
    const hbrick::MazeLayout grid(32U, 32U, true);
    const uint64_t pairs = grid.passableAdjacentPairCount();
    EXPECT_EQ(pairs, 2U * 32U * 32U - 32U - 32U);

    hbrick::RandomAsymmetricParams params{};
    params.seed = 0x48425601ULL;
    params.p_one_way = 0.05L;
    params.p_bidirectional = 0.0L;
    params.p_edge_drop = 0.0L;

    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        params
    );

    std::string error;
    ASSERT_TRUE(hbrick::randomAsymmetricArcCountMatchesExpectation(
        graph.numEdges(),
        pairs,
        params,
        error
    )) << error;

    const double ratio =
        static_cast<double>(graph.numEdges()) / static_cast<double>(pairs);
    EXPECT_GT(ratio, 1.85);
    EXPECT_LT(ratio, 2.0);

    hbrick::RandomAsymmetricParams legacy = params;
    legacy.p_edge_drop = -1.0L;
    const hbrick::DirectedGridGraph sparse = hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        legacy
    );
    const double sparse_ratio =
        static_cast<double>(sparse.numEdges()) / static_cast<double>(pairs);
    EXPECT_GT(sparse_ratio, 0.03);
    EXPECT_LT(sparse_ratio, 0.08);
}
