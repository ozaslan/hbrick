/**
 * @file benchmark_campaign_run.hpp
 * @ingroup hbrick_bench
 * @brief Manifest-driven benchmark execution for CLI campaigns.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hbrick/bench/benchmark_campaign.hpp"
#include "hbrick/bench/benchmark_campaign_log.hpp"
#include "hbrick/bench/benchmark_campaign_dataset.hpp"
#include "hbrick/bench/reachability_benchmark.hpp"

namespace hbrick {

/**
 * @brief One parsed manifest row (schema v2 procedural maps).
 * @ingroup hbrick_bench
 */
struct BenchmarkCampaignManifestEntry {
    BenchmarkCampaignMapContext map;
    BenchmarkCampaignMapCharacterization characterization;
};

/**
 * @brief Filters and resume behavior for manifest-driven runs.
 * @ingroup hbrick_bench
 */
struct BenchmarkCampaignRunOptions {
    bool resume = false;
    std::vector<std::string> map_ids;
    std::filesystem::path datasets_root_override;
    BenchmarkCampaignLogger* logger = nullptr;
};

/** @brief Parses @p manifest_csv into @p entries (skips header and schema v1 rows). @ingroup hbrick_bench */
[[nodiscard]] bool loadBenchmarkCampaignManifest(
    const std::filesystem::path& manifest_csv,
    std::vector<BenchmarkCampaignManifestEntry>& entries,
    std::string& error_message
);

/**
 * @brief Rebuilds layout and graph for a manifest entry (procedural or MovingAI).
 * @ingroup hbrick_bench
 */
[[nodiscard]] bool rebuildCampaignMapFromManifestEntry(
    const BenchmarkCampaignManifestEntry& entry,
    const std::filesystem::path& datasets_root_override,
    MazeLayout& layout,
    DirectedGridGraph& graph,
    std::string& error_message
);

/**
 * @brief Rebuilds layout and graph for a procedural manifest entry.
 * @ingroup hbrick_bench
 */
[[nodiscard]] bool rebuildProceduralCampaignMap(
    const BenchmarkCampaignManifestEntry& entry,
    MazeLayout& layout,
    DirectedGridGraph& graph,
    std::string& error_message
);

/**
 * @brief Runs benchmarks for every manifest row matching @p options.
 * @ingroup hbrick_bench
 */
[[nodiscard]] bool runBenchmarkCampaignFromManifest(
    const BenchmarkCampaignPaths& paths,
    const BenchmarkCampaignMetadata& metadata,
    ReachabilityBenchmarkConfig config,
    const BenchmarkCampaignRunOptions& options,
    std::string& error_message
);

}  // namespace hbrick
