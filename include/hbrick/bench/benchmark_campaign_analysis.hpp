/**
 * @file benchmark_campaign_analysis.hpp
 * @ingroup hbrick_bench
 * @brief Map classification and summary regeneration for benchmark campaigns.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "hbrick/bench/benchmark_campaign.hpp"

namespace hbrick {

/** @brief Derives an analysis bucket from generator type and manifest stats. @ingroup hbrick_bench */
[[nodiscard]] std::string benchmarkCampaignMapClass(
    const std::string& generator_type,
    const BenchmarkCampaignMapCharacterization& characterization
) noexcept;

/**
 * @brief Rewrites @c summary.md from the full @c results.csv on disk.
 * @ingroup hbrick_bench
 */
[[nodiscard]] bool regenerateBenchmarkCampaignSummaryFromResults(
    const std::filesystem::path& results_csv,
    const std::filesystem::path& summary_md,
    const BenchmarkCampaignMetadata& metadata,
    std::string& error_message
);

/** @brief Expected header line for schema v2 results (golden CI check). @ingroup hbrick_bench */
[[nodiscard]] const char* benchmarkCampaignResultsCsvHeaderLine() noexcept;

}  // namespace hbrick
