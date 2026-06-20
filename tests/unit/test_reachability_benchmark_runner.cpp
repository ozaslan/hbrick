#include <gtest/gtest.h>

#include <chrono>
#include <limits>
#include <numeric>
#include <thread>

#include "hbrick/bench/reachability_benchmark_runner.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"

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

void waitForRunner(hbrick::ReachabilityBenchmarkRunner& runner) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (runner.workerRunning()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            FAIL() << "ReachabilityBenchmarkRunner timed out";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    runner.reapWorker();
}

[[nodiscard]] hbrick::ReachabilityBenchmarkConfig fastDiamondConfig() {
    hbrick::ReachabilityBenchmarkConfig config;
    config.methods = {
        hbrick::ReachabilityBaselineId::CsrBfs,
        hbrick::ReachabilityBaselineId::CsrDfs,
    };
    config.query_count = 16U;
    config.warmup_queries = 2U;
    config.correctness_check_count = 4U;
    config.queries_per_step = 4U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();
    return config;
}

}  // namespace

TEST(ReachabilityBenchmarkRunner, BackgroundRunProducesValidReport) {
    const hbrick::CsrGraph graph = buildDiamondGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityBenchmarkRunner runner;
    runner.begin(graph, universe, fastDiamondConfig());
    ASSERT_TRUE(runner.active());

    waitForRunner(runner);

    EXPECT_FALSE(runner.active());
    EXPECT_FALSE(runner.workerRunning());
    EXPECT_TRUE(runner.report().valid);
    EXPECT_EQ(runner.report().methods.size(), 2U);
    EXPECT_GT(runner.elapsedSeconds(), 0.0);
}

TEST(ReachabilityBenchmarkRunner, BackgroundRunMatchesBlockingJob) {
    const hbrick::CsrGraph graph = buildDiamondGraph();
    const std::vector<uint32_t> universe = allVertices(graph);
    const hbrick::ReachabilityBenchmarkConfig config = fastDiamondConfig();

    const hbrick::ReachabilityBenchmarkReport blocking =
        hbrick::ReachabilityBenchmarkJob::run(graph, universe, config);

    hbrick::ReachabilityBenchmarkRunner runner;
    runner.begin(graph, universe, config);
    waitForRunner(runner);

    const hbrick::ReachabilityBenchmarkReport background = runner.report();
    ASSERT_TRUE(blocking.valid);
    ASSERT_TRUE(background.valid);
    ASSERT_EQ(blocking.methods.size(), background.methods.size());

    for (std::size_t index = 0; index < blocking.methods.size(); ++index) {
        EXPECT_EQ(blocking.methods[index].status, background.methods[index].status);
        EXPECT_EQ(
            blocking.methods[index].reachable_count,
            background.methods[index].reachable_count
        );
        EXPECT_EQ(
            blocking.methods[index].correctness_mismatches,
            background.methods[index].correctness_mismatches
        );
    }
}

TEST(ReachabilityBenchmarkRunner, CancelStopsWorkerWithoutValidReport) {
    const hbrick::CsrGraph graph = buildDiamondGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityBenchmarkConfig config = fastDiamondConfig();
    config.methods = hbrick::ReachabilityBenchmarkConfig::allMethods().methods;
    config.query_count = 256U;

    hbrick::ReachabilityBenchmarkRunner runner;
    runner.begin(graph, universe, config);
    ASSERT_TRUE(runner.active());

    runner.cancel();
    waitForRunner(runner);

    EXPECT_FALSE(runner.active());
    EXPECT_FALSE(runner.report().valid);
    EXPECT_GT(runner.elapsedSeconds(), 0.0);
}

TEST(ReachabilityBenchmarkRunner, RequestSkipCurrentMethodIsSafeWhileIdle) {
    hbrick::ReachabilityBenchmarkRunner runner;
    runner.requestSkipCurrentMethod();
    EXPECT_FALSE(runner.active());
    EXPECT_DOUBLE_EQ(runner.elapsedSeconds(), 0.0);
}

TEST(ReachabilityBenchmarkRunner, ReapWorkerIsIdempotentAfterCompletion) {
    const hbrick::CsrGraph graph = buildDiamondGraph();
    const std::vector<uint32_t> universe = allVertices(graph);

    hbrick::ReachabilityBenchmarkRunner runner;
    runner.begin(graph, universe, fastDiamondConfig());
    waitForRunner(runner);

    runner.reapWorker();
    runner.reapWorker();
    EXPECT_TRUE(runner.report().valid);
}
