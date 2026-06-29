/**
 * @file benchmark_campaign.hpp
 * @ingroup hbrick_bench
 * @brief On-disk layout, schema, and writers for CLI benchmark campaigns.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "hbrick/bench/benchmark_campaign_config.hpp"
#include "hbrick/bench/reachability_benchmark.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace hbrick {

class BenchmarkCampaignLogger;

/** @brief Schema version for campaign CSV/JSON artifacts. @ingroup hbrick_bench */
constexpr const char* kBenchmarkCampaignSchemaVersion = "2";

/**
 * @brief Standard paths under a campaign root directory.
 * @ingroup hbrick_bench
 */
struct BenchmarkCampaignPaths {
    std::filesystem::path root;
    std::filesystem::path manifest_csv;
    std::filesystem::path results_csv;
    std::filesystem::path summary_md;
    std::filesystem::path metadata_json;
    std::filesystem::path workload_json;
    std::filesystem::path gallery_dir;
    std::filesystem::path recipes_dir;
    std::filesystem::path logs_dir;
};

/**
 * @brief Environment and build metadata recorded once per campaign.
 * @ingroup hbrick_bench
 */
struct BenchmarkCampaignMetadata {
    std::string schema_version = kBenchmarkCampaignSchemaVersion;
    std::string campaign_id;
    std::string started_at_utc;
    std::string git_commit;
    std::string build_type;
    std::string compiler_id;
    std::string compiler_version;
    std::string cpu_model;
    uint32_t hardware_concurrency = 0U;
};

/**
 * @brief Shared query workload independent of map generation.
 * @ingroup hbrick_bench
 */
struct BenchmarkCampaignQueryWorkload {
    uint64_t pair_seed = 0U;
    uint32_t query_count = 0U;
    uint32_t warmup_queries = 0U;
    uint32_t correctness_check_count = 0U;
    uint64_t pair_list_hash = 0U;
};

/**
 * @brief Identifies one map row in the campaign manifest and results.
 * @ingroup hbrick_bench
 */
struct BenchmarkCampaignMapContext {
    std::string map_id;
    std::string generator_type;
    std::string recipe_path;
    std::string gallery_image_path;
};

/**
 * @brief Graph and grid statistics recorded in the campaign manifest.
 * @ingroup hbrick_bench
 */
struct BenchmarkCampaignMapCharacterization {
    uint32_t grid_width = 0U;
    uint32_t grid_height = 0U;
    uint32_t passable_cells = 0U;
    uint32_t num_vertices = 0U;
    uint64_t num_edges = 0U;
    uint32_t undirected_component_count = 0U;
    uint32_t largest_undirected_component = 0U;
    uint32_t num_strongly_connected_components = 0U;
    uint32_t largest_strongly_connected_component = 0U;
    uint32_t condensation_vertices = 0U;
    uint32_t condensation_edges = 0U;
    std::string orientation_mode;
    uint64_t carve_seed = 0U;
    uint64_t opening_seed = 0U;
    uint32_t extra_openings = 0U;
    float p_one_way = 0.0F;
    float p_bidirectional = 0.0F;
    float gradient_angle_degrees = 0.0F;
    float p_against_gradient = 0.0F;
    uint64_t orientation_seed = 0U;
    uint64_t gallery_image_hash = 0U;
    std::string movingai_set;
    std::string movingai_map;
    std::string datasets_root;
    std::string passability_policy;
};

/**
 * @brief One results row: map context + benchmark metrics + workload linkage.
 * @ingroup hbrick_bench
 */
struct BenchmarkCampaignResultRow {
    BenchmarkCampaignMapContext map;
    BaselineBenchmarkMetrics metrics;
    BenchmarkCampaignQueryWorkload workload;
    BenchmarkCampaignRunParameters run_parameters;
    std::string map_class;
    uint32_t passable_cells = 0U;
    uint32_t num_sccs = 0U;
    std::string campaign_id;
    std::string run_timestamp_utc;
    uint32_t num_vertices = 0U;
    uint64_t num_edges = 0U;
    uint64_t peak_preprocess_rss_bytes = 0U;
};

/** @brief Returns canonical paths under @p campaign_root. @ingroup hbrick_bench */
[[nodiscard]] BenchmarkCampaignPaths benchmarkCampaignPathsFromRoot(
    const std::filesystem::path& campaign_root
);

/**
 * @brief Creates campaign subdirectories and empty manifest/results headers.
 * @ingroup hbrick_bench
 */
[[nodiscard]] bool initializeBenchmarkCampaignDirectory(
    const BenchmarkCampaignPaths& paths,
    BenchmarkCampaignMetadata metadata,
    std::string& error_message
);

/** @brief Samples host/build metadata for @c metadata.json. @ingroup hbrick_bench */
[[nodiscard]] BenchmarkCampaignMetadata captureBenchmarkCampaignMetadata(
    std::string campaign_id
);

/** @brief Builds a workload record from @p report and @p config. @ingroup hbrick_bench */
[[nodiscard]] BenchmarkCampaignQueryWorkload workloadFromBenchmarkReport(
    const ReachabilityBenchmarkReport& report,
    const ReachabilityBenchmarkConfig& config
) noexcept;

/** @brief Converts a @ref ReachabilityBenchmarkReport into campaign result rows. @ingroup hbrick_bench */
[[nodiscard]] std::vector<BenchmarkCampaignResultRow> benchmarkCampaignRowsFromReport(
    const BenchmarkCampaignMapContext& map,
    const BenchmarkCampaignMetadata& metadata,
    const BenchmarkCampaignQueryWorkload& workload,
    const ReachabilityBenchmarkReport& report,
    const ReachabilityBenchmarkConfig& config,
    uint64_t peak_preprocess_rss_bytes,
    const std::string& map_class = {},
    const BenchmarkCampaignMapCharacterization* map_characterization = nullptr
);

/** @brief Writes or overwrites @c workload.json. @ingroup hbrick_bench */
[[nodiscard]] bool writeBenchmarkCampaignWorkloadJson(
    const std::filesystem::path& workload_json,
    const BenchmarkCampaignQueryWorkload& workload,
    std::string& error_message
);

/** @brief Appends one manifest row for @p map (legacy fields only). @ingroup hbrick_bench */
[[nodiscard]] bool appendBenchmarkCampaignManifestCsv(
    const std::filesystem::path& manifest_csv,
    const BenchmarkCampaignMetadata& metadata,
    const BenchmarkCampaignMapContext& map,
    uint32_t num_vertices,
    uint64_t num_edges,
    std::string& error_message
);

/** @brief Appends a fully characterized manifest row. @ingroup hbrick_bench */
[[nodiscard]] bool appendBenchmarkCampaignManifestRowFull(
    const std::filesystem::path& manifest_csv,
    const BenchmarkCampaignMetadata& metadata,
    const BenchmarkCampaignMapContext& map,
    const BenchmarkCampaignMapCharacterization& characterization,
    std::string& error_message
);

/** @brief Appends result rows to @c results.csv. @ingroup hbrick_bench */
[[nodiscard]] bool appendBenchmarkCampaignResultsCsv(
    const std::filesystem::path& results_csv,
    std::span<const BenchmarkCampaignResultRow> rows,
    std::string& error_message
);

/** @brief Regenerates @c summary.md from result rows. @ingroup hbrick_bench */
[[nodiscard]] bool writeBenchmarkCampaignSummaryMd(
    const std::filesystem::path& summary_md,
    const BenchmarkCampaignMetadata& metadata,
    std::span<const BenchmarkCampaignResultRow> rows,
    std::string& error_message
);

/** @brief Human-readable @ref BaselineStatus label for CSV output. @ingroup hbrick_bench */
[[nodiscard]] const char* baselineStatusLabel(BaselineStatus status) noexcept;

/**
 * @brief Runs @ref ReachabilityBenchmarkJob and appends campaign outputs.
 * @ingroup hbrick_bench
 *
 * When @p append_manifest is @c false the manifest is left unchanged (maps
 * pre-generated via @ref generateProceduralCampaignMaps).
 */
[[nodiscard]] bool runBenchmarkCampaignGridJob(
    const BenchmarkCampaignPaths& paths,
    const BenchmarkCampaignMetadata& metadata,
    const BenchmarkCampaignMapContext& map,
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    ReachabilityBenchmarkConfig config,
    std::string& error_message,
    bool append_manifest = true,
    BenchmarkCampaignLogger* logger = nullptr,
    const std::string& map_class = {},
    const BenchmarkCampaignMapCharacterization* map_characterization = nullptr
);

}  // namespace hbrick
