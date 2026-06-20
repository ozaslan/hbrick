/**
 * @file reachability_benchmark_runner.hpp
 * @ingroup hbrick_bench
 * @brief Background-thread wrapper for @ref ReachabilityBenchmarkJob.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "hbrick/bench/reachability_benchmark.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace hbrick {

/**
 * @brief Runs @ref ReachabilityBenchmarkJob on a worker thread with cancel/skip controls.
 * @ingroup hbrick_bench
 */
class ReachabilityBenchmarkRunner {
public:
    ReachabilityBenchmarkRunner();
    ~ReachabilityBenchmarkRunner();

    ReachabilityBenchmarkRunner(const ReachabilityBenchmarkRunner&) = delete;
    ReachabilityBenchmarkRunner& operator=(const ReachabilityBenchmarkRunner&) = delete;
    ReachabilityBenchmarkRunner(ReachabilityBenchmarkRunner&&) noexcept;
    ReachabilityBenchmarkRunner& operator=(ReachabilityBenchmarkRunner&&) noexcept;

    void begin(
        const CsrGraph& graph,
        std::span<const uint32_t> query_universe,
        ReachabilityBenchmarkConfig config
    );

    void begin(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        std::span<const uint32_t> query_universe,
        ReachabilityBenchmarkConfig config
    );

    void cancel() noexcept;
    void requestSkipCurrentMethod() noexcept;
    void reapWorker() noexcept;

    [[nodiscard]] bool workerRunning() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] double elapsedSeconds() const noexcept;

    [[nodiscard]] const ReachabilityBenchmarkReport& report() const noexcept;
    [[nodiscard]] ReachabilityBenchmarkProgress progress() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace hbrick
