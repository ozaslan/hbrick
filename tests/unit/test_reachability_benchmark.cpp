#include <gtest/gtest.h>

#include <limits>
#include <numeric>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/baselines/two_hop_baseline.hpp"
#include "hbrick/bench/reachability_benchmark.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace {

hbrick::CsrGraph buildDiamondGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 2U);
    builder.addEdge(1U, 3U);
    builder.addEdge(2U, 3U);
    return builder.build();
}

[[nodiscard]] std::vector<uint32_t> allVertices(const hbrick::CsrGraph& graph) {
    std::vector<uint32_t> vertices(graph.numVertices());
    std::iota(vertices.begin(), vertices.end(), 0U);
    return vertices;
}

}  // namespace

TEST(ReachabilityBenchmark, RunProducesMatchingResultsOnDiamondGraph) {
    const hbrick::CsrGraph graph = buildDiamondGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityBenchmarkConfig config =
        hbrick::ReachabilityBenchmarkConfig::allMethods();
    config.query_count = 128U;
    config.warmup_queries = 8U;
    config.correctness_check_count = 32U;
    config.pair_seed = 0x1234ULL;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    const hbrick::ReachabilityBenchmarkReport report =
        hbrick::ReachabilityBenchmarkJob::run(graph, universe, config);

    ASSERT_TRUE(report.valid);
    EXPECT_EQ(report.num_vertices, 4U);
    EXPECT_EQ(report.query_pair_count, 128U);
    ASSERT_EQ(report.methods.size(), 10U);

    double reachable_ratio = -1.0;
    for (const hbrick::BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.method == hbrick::ReachabilityBaselineId::BrickSearch
            || metrics.method == hbrick::ReachabilityBaselineId::BrickClosure
            || metrics.method == hbrick::ReachabilityBaselineId::HBrick) {
            EXPECT_EQ(metrics.status, hbrick::BaselineStatus::SkippedByPolicy);
            EXPECT_FALSE(metrics.policy_skip_detail.empty());
            continue;
        }
        EXPECT_EQ(metrics.status, hbrick::BaselineStatus::Completed);
        EXPECT_GT(metrics.query_stats.count, 0U);
        EXPECT_GT(metrics.query_stats.mean_nanoseconds, 0.0);
        EXPECT_EQ(metrics.correctness_mismatches, 0U);
        EXPECT_GT(metrics.correctness_checks, 0U);

        if (reachable_ratio < 0.0) {
            reachable_ratio = metrics.reachable_ratio;
        } else {
            EXPECT_NEAR(metrics.reachable_ratio, reachable_ratio, 1.0e-9);
        }
    }

    EXPECT_GT(report.reference_bfs_mean_query_nanoseconds, 0.0);
    EXPECT_GT(report.reference_bfs_total_benchmark_nanoseconds, 0U);

    for (const hbrick::BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.method == hbrick::ReachabilityBaselineId::BrickSearch
            || metrics.method == hbrick::ReachabilityBaselineId::BrickClosure
            || metrics.method == hbrick::ReachabilityBaselineId::HBrick) {
            continue;
        }
        EXPECT_GT(metrics.total_benchmark_nanoseconds, 0U);
        if (metrics.method == hbrick::ReachabilityBaselineId::CsrBfs) {
            EXPECT_NEAR(metrics.speedup_vs_bfs, 1.0, 1.0e-9);
            EXPECT_NEAR(metrics.total_speedup_vs_bfs, 1.0, 1.0e-9);
        }
    }
}

TEST(ReachabilityBenchmark, IncrementalJobMatchesBlockingRun) {
    const hbrick::MazeLayout layout{4U, 3U};
    const hbrick::DirectedGridGraph directed =
        hbrick::DirectedGridGraphBuilder::build(
            layout,
            hbrick::GridEdgeConversionMode::RandomAsymmetric,
            hbrick::RandomAsymmetricParams{0xABCDULL, 0.20L, 0.15L}
        );
    const hbrick::CsrGraph& graph = directed.csrGraph();

    std::vector<uint32_t> universe;
    universe.reserve(layout.passableCount());
    for (uint32_t vertex = 0U; vertex < layout.numVertices(); ++vertex) {
        if (layout.isPassable(hbrick::VertexId{vertex})) {
            universe.push_back(vertex);
        }
    }

    hbrick::ReachabilityBenchmarkConfig config =
        hbrick::ReachabilityBenchmarkConfig::allMethods();
    config.query_count = 64U;
    config.warmup_queries = 4U;
    config.correctness_check_count = 16U;
    config.queries_per_step = 8U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    const hbrick::ReachabilityBenchmarkReport blocking =
        hbrick::ReachabilityBenchmarkJob::run(graph, universe, config);

    hbrick::ReachabilityBenchmarkJob job;
    job.begin(graph, universe, config);
    while (!job.step()) {
    }

    const hbrick::ReachabilityBenchmarkReport incremental = job.report();
    ASSERT_TRUE(blocking.valid);
    ASSERT_TRUE(incremental.valid);
    ASSERT_EQ(blocking.methods.size(), incremental.methods.size());

    for (std::size_t index = 0; index < blocking.methods.size(); ++index) {
        EXPECT_EQ(
            blocking.methods[index].status,
            incremental.methods[index].status
        );
        EXPECT_EQ(
            blocking.methods[index].reachable_count,
            incremental.methods[index].reachable_count
        );
        EXPECT_EQ(
            blocking.methods[index].correctness_mismatches,
            incremental.methods[index].correctness_mismatches
        );
    }
}

TEST(ReachabilityBenchmark, FullClosureRunsLastInDefaultOrder) {
    const hbrick::ReachabilityBenchmarkConfig config =
        hbrick::ReachabilityBenchmarkConfig::allMethods();

    ASSERT_FALSE(config.methods.empty());
    EXPECT_EQ(
        config.methods.back(),
        hbrick::ReachabilityBaselineId::FullClosure
    );
}

TEST(ReachabilityBenchmark, SkippedBaselineStillReportsStatus) {
    hbrick::CsrGraphBuilder builder{65U};
    const hbrick::CsrGraph graph = builder.build();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityBenchmarkConfig config;
    config.methods = {hbrick::ReachabilityBaselineId::TwoHop};
    config.query_count = 8U;
    config.warmup_queries = 0U;
    config.correctness_check_count = 0U;
    config.max_memory_bytes = 16U;

    const hbrick::ReachabilityBenchmarkReport report =
        hbrick::ReachabilityBenchmarkJob::run(graph, universe, config);

    ASSERT_TRUE(report.valid);
    ASSERT_EQ(report.methods.size(), 1U);
    EXPECT_EQ(report.methods.front().status, hbrick::BaselineStatus::SkippedByPolicy);
    EXPECT_EQ(report.methods.front().query_stats.count, 0U);
    EXPECT_FALSE(report.methods.front().policy_skip_detail.empty());
}

TEST(ReachabilityBenchmark, BrickBaselinesCompleteOnSmallGrid) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const std::vector<uint32_t> universe = allVertices(graph.csrGraph());

    hbrick::ReachabilityBenchmarkConfig config;
    config.methods = {
        hbrick::ReachabilityBaselineId::BrickSearch,
        hbrick::ReachabilityBaselineId::BrickClosure,
    };
    config.query_count = 32U;
    config.warmup_queries = 4U;
    config.correctness_check_count = 16U;
    config.pair_seed = 0xB11C0001ULL;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();
    config.brick_tile_size = hbrick::TileSize{4U, 4U};

    const hbrick::ReachabilityBenchmarkReport report =
        hbrick::ReachabilityBenchmarkJob::run(graph, layout, universe, config);

    ASSERT_TRUE(report.valid);
    ASSERT_EQ(report.methods.size(), 2U);
    for (const hbrick::BaselineBenchmarkMetrics& metrics : report.methods) {
        EXPECT_EQ(metrics.status, hbrick::BaselineStatus::Completed);
        EXPECT_EQ(metrics.correctness_mismatches, 0U);
        EXPECT_GT(metrics.query_stats.count, 0U);
    }
}

TEST(ReachabilityBenchmark, HBrickBaselineCompletesOnSmallGrid) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const std::vector<uint32_t> universe = allVertices(graph.csrGraph());

    hbrick::ReachabilityBenchmarkConfig config;
    config.methods = {hbrick::ReachabilityBaselineId::HBrick};
    config.query_count = 32U;
    config.warmup_queries = 4U;
    config.correctness_check_count = 16U;
    config.pair_seed = 0x1B11C001ULL;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();
    config.brick_tile_size = hbrick::TileSize{4U, 4U};
    config.hbrick_group_size = hbrick::GroupSize{2U, 2U};
    config.hbrick_max_depth = 2U;

    const hbrick::ReachabilityBenchmarkReport report =
        hbrick::ReachabilityBenchmarkJob::run(graph, layout, universe, config);

    ASSERT_TRUE(report.valid);
    ASSERT_EQ(report.methods.size(), 1U);
    EXPECT_EQ(report.methods.front().method, hbrick::ReachabilityBaselineId::HBrick);
    EXPECT_EQ(report.methods.front().status, hbrick::BaselineStatus::Completed);
    EXPECT_EQ(report.methods.front().correctness_mismatches, 0U);
    EXPECT_GT(report.methods.front().query_stats.count, 0U);
    EXPECT_GT(report.methods.front().estimated_index_bytes, 0U);
}
