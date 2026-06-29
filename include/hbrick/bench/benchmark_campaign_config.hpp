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
#include "hbrick/tile/hbrick_config.hpp"

namespace hbrick {

/**
 * @brief Benchmark parameter tuple recorded in @c results.csv for resume and analysis.
 * @ingroup hbrick_bench
 */
struct BenchmarkCampaignRunParameters {
    std::string config_id;
    uint32_t brick_tile_width = 0U;
    uint32_t brick_tile_height = 0U;
    uint64_t max_memory_bytes = 0U;
    bool kleene_use_parallel = false;
    uint32_t kleene_num_threads = 0U;
    uint32_t hbrick_group_width = 0U;
    uint32_t hbrick_group_height = 0U;
    uint32_t hbrick_max_depth = 0U;
    bool closure_early_stop = false;
};

/** @brief Stable id for @p config (tile, Kleene, H-BRICK, memory knobs). @ingroup hbrick_bench */
[[nodiscard]] std::string benchmarkCampaignConfigId(
    const ReachabilityBenchmarkConfig& config
) noexcept;

/** @brief Copies tunable fields from @p config into a run-parameters snapshot. @ingroup hbrick_bench */
[[nodiscard]] BenchmarkCampaignRunParameters benchmarkCampaignRunParametersFromConfig(
    const ReachabilityBenchmarkConfig& config
) noexcept;

/**
 * @brief Expands @p base into one or more configs for a named sweep.
 * @ingroup hbrick_bench
 *
 * Sweeps: @c default, @c brick, @c brick-tile, @c brick-kleene, @c hbrick, @c all.
 */
[[nodiscard]] std::vector<ReachabilityBenchmarkConfig> expandBenchmarkCampaignConfigSweep(
    const ReachabilityBenchmarkConfig& base,
    const std::string& sweep_name,
    std::string& error_message
);

/**
 * @brief Union of expansions for each sweep name in @p sweep_names.
 * @ingroup hbrick_bench
 */
[[nodiscard]] std::vector<ReachabilityBenchmarkConfig> expandBenchmarkCampaignConfigSweeps(
    const ReachabilityBenchmarkConfig& base,
    std::span<const std::string> sweep_names,
    std::string& error_message
);

/** @brief Parses comma-separated sweep names from @c --configs. @ingroup hbrick_bench */
[[nodiscard]] bool parseBenchmarkCampaignConfigSweepNames(
    const std::string& csv,
    std::vector<std::string>& sweep_names,
    std::string& error_message
);

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
 * Presets: @c smoke, @c csr, @c brick, @c kleene-oracle, @c all.
 */
[[nodiscard]] ReachabilityBenchmarkConfig benchmarkCampaignConfigFromPreset(
    const char* preset
) noexcept;

}  // namespace hbrick
