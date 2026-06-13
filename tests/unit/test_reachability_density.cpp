#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/reachability_density.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "maze_generator.hpp"

namespace {

hbrick::CsrGraph buildDirectedPairGraph() {
    hbrick::CsrGraphBuilder builder{2U};
    builder.addEdge(0U, 1U);
    return builder.build();
}

hbrick::CsrGraph buildMutualCycleGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(1U, 2U);
    builder.addEdge(2U, 3U);
    builder.addEdge(3U, 0U);
    return builder.build();
}

[[nodiscard]] std::vector<uint32_t> allVertices(const hbrick::CsrGraph& graph) {
    std::vector<uint32_t> vertices(graph.numVertices());
    for (uint32_t vertex = 0; vertex < graph.numVertices(); ++vertex) {
        vertices[vertex] = vertex;
    }
    return vertices;
}

hbrick::CsrGraph buildLongChainGraph() {
    constexpr uint32_t kVertices = 40U;
    hbrick::CsrGraphBuilder builder{kVertices};
    for (uint32_t vertex = 0; vertex + 1U < kVertices; ++vertex) {
        builder.addEdge(vertex, vertex + 1U);
    }
    return builder.build();
}

}  // namespace

TEST(BfsReachableCount, CountsForwardReachableVertices) {
    const hbrick::CsrGraph graph = buildDirectedPairGraph();
    hbrick::GraphSearchScratch scratch(graph.numVertices());

    EXPECT_EQ(hbrick::Bfs::reachableCount(graph, 0U, scratch), 2U);
    EXPECT_EQ(hbrick::Bfs::reachableCount(graph, 1U, scratch), 1U);
    EXPECT_EQ(hbrick::Bfs::reachableCount(graph, 99U, scratch), 0U);
}

TEST(ReachabilityDensity, ExactEstimateOverSmallUniverse) {
    const hbrick::CsrGraph graph = buildDirectedPairGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityDensityConfig config;
    config.max_samples = 512U;
    config.sample_mode = hbrick::ReachabilityDensitySampleMode::FixedSamples;

    hbrick::ReachabilityDensityEstimator estimator;
    const hbrick::ReachabilityDensityEstimate result =
        estimator.estimate(graph, universe, config);

    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.samples, 2U);
    EXPECT_FLOAT_EQ(result.density, 0.75F);
    EXPECT_FLOAT_EQ(result.std_error, 0.0F);
}

TEST(ReachabilityDensity, FixedSamplesRunsRequestedCountOnLargeUniverse) {
    const hbrick::CsrGraph graph = buildLongChainGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityDensityConfig config;
    config.max_samples = 32U;
    config.sample_mode = hbrick::ReachabilityDensitySampleMode::FixedSamples;
    config.rng_seed = 42U;

    hbrick::ReachabilityDensityEstimator estimator;
    const hbrick::ReachabilityDensityEstimate result =
        estimator.estimate(graph, universe, config);

    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.samples, 32U);
    EXPECT_GT(result.std_error, 0.0F);
}

TEST(ReachabilityDensity, SampledSourcesAreDistinctWithoutReplacement) {
    const hbrick::CsrGraph graph = buildLongChainGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityDensityConfig config;
    config.max_samples = 32U;
    config.sample_mode = hbrick::ReachabilityDensitySampleMode::FixedSamples;
    config.rng_seed = 42U;

    hbrick::ReachabilityDensityEstimator estimator;
    estimator.begin(graph, universe, config);

    std::vector<uint32_t> sampled(
        estimator.universe().begin(),
        estimator.universe().begin()
            + static_cast<std::ptrdiff_t>(estimator.source_count())
    );
    std::sort(sampled.begin(), sampled.end());
    const auto unique_end = std::unique(sampled.begin(), sampled.end());
    EXPECT_EQ(
        static_cast<std::size_t>(std::distance(sampled.begin(), unique_end)),
        sampled.size()
    );
}

TEST(ReachabilityDensity, SameSeedProducesSameSourceOrder) {
    const hbrick::CsrGraph graph = buildLongChainGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityDensityConfig config;
    config.max_samples = 16U;
    config.sample_mode = hbrick::ReachabilityDensitySampleMode::FixedSamples;
    config.rng_seed = 12345U;

    hbrick::ReachabilityDensityEstimator first;
    first.begin(graph, universe, config);
    const std::vector<uint32_t> first_order(
        first.universe().begin(),
        first.universe().begin()
            + static_cast<std::ptrdiff_t>(first.source_count())
    );

    hbrick::ReachabilityDensityConfig other = config;
    other.rng_seed = 999U;
    hbrick::ReachabilityDensityEstimator second;
    second.begin(graph, universe, other);
    const std::vector<uint32_t> second_order(
        second.universe().begin(),
        second.universe().begin()
            + static_cast<std::ptrdiff_t>(second.source_count())
    );

    hbrick::ReachabilityDensityEstimator third;
    third.begin(graph, universe, config);
    const std::vector<uint32_t> third_order(
        third.universe().begin(),
        third.universe().begin()
            + static_cast<std::ptrdiff_t>(third.source_count())
    );

    EXPECT_EQ(first_order, third_order);
    EXPECT_NE(first_order, second_order);
}

TEST(ReachabilityDensity, ParallelEstimateMatchesSerialForSameSeed) {
    const hbrick::CsrGraph graph = buildLongChainGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityDensityConfig serial_config;
    serial_config.max_samples = 32U;
    serial_config.sample_mode = hbrick::ReachabilityDensitySampleMode::FixedSamples;
    serial_config.rng_seed = 2024U;
    serial_config.num_threads = 1U;

    hbrick::ReachabilityDensityConfig parallel_config = serial_config;
    parallel_config.num_threads = 4U;

    hbrick::ReachabilityDensityEstimator serial;
    const hbrick::ReachabilityDensityEstimate serial_result =
        serial.estimate(graph, universe, serial_config);

    hbrick::ReachabilityDensityEstimator parallel;
    const hbrick::ReachabilityDensityEstimate parallel_result =
        parallel.estimate(graph, universe, parallel_config);

    ASSERT_TRUE(serial_result.valid);
    ASSERT_TRUE(parallel_result.valid);
    EXPECT_FLOAT_EQ(parallel_result.density, serial_result.density);
    EXPECT_FLOAT_EQ(parallel_result.std_error, serial_result.std_error);
    EXPECT_EQ(parallel_result.samples, serial_result.samples);
}

TEST(ReachabilityDensity, ParallelBatchUsesDistinctUpcomingSources) {
    const hbrick::CsrGraph graph = buildLongChainGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityDensityConfig config;
    config.max_samples = 16U;
    config.sample_mode = hbrick::ReachabilityDensitySampleMode::FixedSamples;
    config.rng_seed = 77U;
    config.num_threads = 4U;

    hbrick::ReachabilityDensityEstimator estimator;
    estimator.begin(graph, universe, config);
    ASSERT_EQ(estimator.num_threads(), 4U);

    const uint32_t completed_before = estimator.completed();
    ASSERT_FALSE(estimator.stepParallel(graph, 4U));
    EXPECT_EQ(estimator.completed(), completed_before + 4U);

    std::vector<uint32_t> used(
        estimator.universe().begin(),
        estimator.universe().begin()
            + static_cast<std::ptrdiff_t>(estimator.completed())
    );
    std::sort(used.begin(), used.end());
    const auto unique_end = std::unique(used.begin(), used.end());
    EXPECT_EQ(
        static_cast<std::size_t>(std::distance(used.begin(), unique_end)),
        used.size()
    );
}

TEST(ReachabilityDensity, AutoStopCanFinishBeforeSampleCap) {
    const hbrick::CsrGraph graph = buildMutualCycleGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityDensityConfig config;
    config.max_samples = 512U;
    config.sample_mode = hbrick::ReachabilityDensitySampleMode::AutoStopWhenStable;
    config.rng_seed = 7U;

    hbrick::ReachabilityDensityEstimator estimator;
    estimator.begin(graph, universe, config);
    while (estimator.active()) {
        estimator.step(graph);
    }

    const hbrick::ReachabilityDensityEstimate result = estimator.result();
    ASSERT_TRUE(result.valid);
    EXPECT_FLOAT_EQ(result.density, 1.0F);
    EXPECT_LT(result.samples, config.max_samples);
}

TEST(ReachabilityDensity, StopKeepsPartialEstimate) {
    const hbrick::CsrGraph graph = buildLongChainGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityDensityConfig config;
    config.max_samples = 256U;
    config.sample_mode = hbrick::ReachabilityDensitySampleMode::FixedSamples;
    config.rng_seed = 11U;

    hbrick::ReachabilityDensityEstimator estimator;
    estimator.begin(graph, universe, config);
    ASSERT_FALSE(estimator.step(graph));
    ASSERT_TRUE(estimator.active());
    estimator.stop();

    const hbrick::ReachabilityDensityEstimate result = estimator.result();
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.samples, 1U);
    EXPECT_FALSE(estimator.active());
}

TEST(ReachabilityDensity, CancelDiscardsActiveJob) {
    const hbrick::CsrGraph graph = buildLongChainGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityDensityConfig config;
    config.max_samples = 64U;
    config.sample_mode = hbrick::ReachabilityDensitySampleMode::FixedSamples;

    hbrick::ReachabilityDensityEstimator estimator;
    estimator.begin(graph, universe, config);
    ASSERT_FALSE(estimator.step(graph));
    estimator.cancel();

    EXPECT_FALSE(estimator.active());
    EXPECT_FALSE(estimator.result().valid);
}

TEST(ReachabilityDensity, IncrementalStepsMatchBlockingEstimate) {
    const hbrick::test_support::MazeParams params{2U, 2U, 0xBEEFULL};
    const hbrick::MazeLayout layout =
        hbrick::test_support::generatePerfectMaze(params);
    const hbrick::CsrGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    ).csrGraph();

    std::vector<uint32_t> passable;
    passable.reserve(layout.passableCount());
    for (uint32_t vertex = 0; vertex < layout.numVertices(); ++vertex) {
        if (layout.isPassable(hbrick::VertexId{vertex})) {
            passable.push_back(vertex);
        }
    }

    hbrick::ReachabilityDensityConfig config;
    config.max_samples = 64U;
    config.sample_mode = hbrick::ReachabilityDensitySampleMode::FixedSamples;
    config.rng_seed = 99U;

    hbrick::ReachabilityDensityEstimator incremental;
    incremental.begin(graph, passable, config);
    while (incremental.active()) {
        incremental.step(graph);
    }

    hbrick::ReachabilityDensityEstimator blocking;
    const hbrick::ReachabilityDensityEstimate blocking_result =
        blocking.estimate(graph, passable, config);

    const hbrick::ReachabilityDensityEstimate incremental_result =
        incremental.result();
    ASSERT_TRUE(blocking_result.valid);
    ASSERT_TRUE(incremental_result.valid);
    EXPECT_FLOAT_EQ(incremental_result.density, blocking_result.density);
    EXPECT_FLOAT_EQ(incremental_result.std_error, blocking_result.std_error);
    EXPECT_EQ(incremental_result.samples, blocking_result.samples);
}
