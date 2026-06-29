/**
 * @file benchmark_campaign_config.hpp
 * @ingroup hbrick_bench
 * @brief CLI method presets and baseline name parsing for benchmark campaigns.
 */

#pragma once

#include <span>
#include <string>
#include <vector>

#include "hbrick/bench/reachability_benchmark.hpp"

namespace hbrick {

/** @brief Parses baseline names such as @c CsrBfs into enum values. @ingroup hbrick_bench */
[[nodiscard]] bool parseReachabilityBaselineNames(
    std::span<const std::string> names,
    std::vector<ReachabilityBaselineId>& methods,
    std::string& error_message
);

/**
 * @brief Parses a comma-separated method list (e.g. @c "CsrBfs,BrickSearch,HBrick").
 * @ingroup hbrick_bench
 */
[[nodiscard]] bool parseReachabilityBaselineCsv(
    const std::string& csv,
    std::vector<ReachabilityBaselineId>& methods,
    std::string& error_message
);

/** @brief Case-insensitive parse of one baseline name; returns @c nullopt on failure. @ingroup hbrick_bench */
[[nodiscard]] bool reachabilityBaselineFromName(
    const std::string& name,
    ReachabilityBaselineId& method
) noexcept;

/**
 * @brief Returns a benchmark config for a named preset.
 * @ingroup hbrick_bench
 *
 * Presets: @c smoke, @c csr, @c brick, @c all.
 */
[[nodiscard]] ReachabilityBenchmarkConfig benchmarkCampaignConfigFromPreset(
    const char* preset
) noexcept;

}  // namespace hbrick
