/**
 * @file reachability_benchmark_runner.hpp
 * @ingroup hbrick_bench
 * @brief Incremental driver for @ref ReachabilityBenchmarkJob.
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
 * @brief Runs @ref ReachabilityBenchmarkJob incrementally via @ref step.
 * @ingroup hbrick_bench
 *
 * Callers (e.g. dataset-browser UI) advance the job on the main thread with a
 * per-frame time budget so live results stay consistent with other incremental jobs.
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

    /**
     * @brief Advances the benchmark by one or more incremental work units.
     * @return @c true when the job has finished or was cancelled.
     */
    [[nodiscard]] bool step() noexcept;

    void cancel() noexcept;
    void requestSkipCurrentMethod() noexcept;
    void reapWorker() noexcept;

    /** @brief Alias for @ref active (legacy name from the threaded driver). */
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
