/**
 * @file reachability_benchmark_util.hpp
 * @ingroup hbrick_bench
 * @brief Report query helpers for reachability benchmark jobs.
 */

#pragma once

#include <cstdint>

#include "hbrick/bench/reachability_benchmark.hpp"

namespace hbrick {

/** @brief Chooses an incremental @c queries_per_step value for @p query_count. @ingroup hbrick_bench */
[[nodiscard]] uint32_t chooseBenchmarkQueriesPerStep(uint32_t query_count) noexcept;

/** @brief Returns @c true when preprocessing builds a persistent index. @ingroup hbrick_bench */
[[nodiscard]] bool baselineBuildsPreprocessIndex(
    ReachabilityBaselineId method
) noexcept;

/** @brief Index of the method column currently running, or @c -1. @ingroup hbrick_bench */
[[nodiscard]] int activeReachabilityBenchmarkMethodIndex(
    const ReachabilityBenchmarkReport& report,
    const ReachabilityBenchmarkProgress& progress,
    bool running
) noexcept;

/** @brief Returns @c true during incremental closure Warshall preprocessing. @ingroup hbrick_bench */
[[nodiscard]] bool closurePreprocessActive(
    const ReachabilityBenchmarkProgress& progress,
    bool running
) noexcept;

/** @brief CsrBfs end-to-end benchmark time used as the total-speedup reference. @ingroup hbrick_bench */
[[nodiscard]] uint64_t referenceBfsTotalNanoseconds(
    const ReachabilityBenchmarkReport& report
) noexcept;

/** @brief CsrBfs mean timed-query latency used as the query-speedup reference. @ingroup hbrick_bench */
[[nodiscard]] double referenceBfsMeanQueryNanoseconds(
    const ReachabilityBenchmarkReport& report
) noexcept;

/** @brief Live or stored mean-query speedup vs CsrBfs for @p metrics. @ingroup hbrick_bench */
[[nodiscard]] double liveQuerySpeedupVsBfs(
    const ReachabilityBenchmarkReport& report,
    const BaselineBenchmarkMetrics& metrics
) noexcept;

}  // namespace hbrick
