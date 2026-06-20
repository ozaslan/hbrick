/**
 * @file reachability_benchmark_format.hpp
 * @ingroup hbrick_bench
 * @brief Text formatting helpers for reachability benchmark reports.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "hbrick/bench/reachability_benchmark.hpp"

namespace hbrick {

/** @brief Formats @p nanoseconds as s / ms / us / ns into @p buffer. @ingroup hbrick_bench */
void formatBenchmarkNanoseconds(
    char* buffer,
    std::size_t size,
    double nanoseconds
) noexcept;

/** @brief Formats @p bytes as TiB / GiB / MiB / KiB / B into @p buffer. @ingroup hbrick_bench */
void formatBenchmarkBytes(char* buffer, std::size_t size, uint64_t bytes) noexcept;

/** @brief Formats large integers with K / M / B / T suffixes. @ingroup hbrick_bench */
void formatBenchmarkLargeCount(char* buffer, std::size_t size, uint64_t value) noexcept;

/** @brief Formats @p completed / @p total using @ref formatBenchmarkLargeCount. @ingroup hbrick_bench */
void formatBenchmarkLargeCountPair(
    char* buffer,
    std::size_t size,
    uint64_t completed,
    uint64_t total
) noexcept;

/** @brief Formats a speedup ratio with adaptive precision. @ingroup hbrick_bench */
void formatBenchmarkSpeedupRatio(
    char* buffer,
    std::size_t size,
    double speedup
) noexcept;

/** @brief Human-readable label for @p stage. @ingroup hbrick_bench */
[[nodiscard]] const char* reachabilityBenchmarkStageLabel(
    ReachabilityBenchmarkProgress::Stage stage
) noexcept;

/** @brief One-line progress detail for the active benchmark stage. @ingroup hbrick_bench */
void formatReachabilityBenchmarkStageDetail(
    char* buffer,
    std::size_t size,
    const ReachabilityBenchmarkProgress& progress
) noexcept;

/** @brief Preprocess column text for benchmark result tables. @ingroup hbrick_bench */
void formatReachabilityBenchmarkPreprocessCell(
    char* buffer,
    std::size_t size,
    const BaselineBenchmarkMetrics& metrics,
    const ReachabilityBenchmarkReport& report,
    const ReachabilityBenchmarkProgress& progress,
    std::size_t method_index,
    bool running
) noexcept;

}  // namespace hbrick
