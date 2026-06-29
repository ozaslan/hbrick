/**
 * @file benchmark_campaign_dataset.hpp
 * @ingroup hbrick_bench
 * @brief Procedural maze generation and map characterization for benchmark campaigns.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

#include "hbrick/bench/benchmark_campaign.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/io/movingai_map.hpp"

namespace hbrick {

/**
 * @brief Parameters for one procedurally generated maze map.
 * @ingroup hbrick_bench
 */
struct ProceduralMazeSpec {
    uint32_t logical_width = 5U;
    uint32_t logical_height = 5U;
    uint64_t carve_seed = 0U;
    uint64_t opening_seed = 0U;
    uint32_t extra_openings = 0U;
    uint64_t orientation_seed = 1U;
    GridEdgeConversionMode orientation_mode = GridEdgeConversionMode::RandomAsymmetric;
    RandomAsymmetricParams asymmetric_params{};
};

/**
 * @brief One generated map ready for manifest export and benchmarking.
 * @ingroup hbrick_bench
 */
struct GeneratedCampaignMap {
    std::string map_id;
    MazeLayout layout{1U, 1U, false};
    DirectedGridGraph graph;
    BenchmarkCampaignMapContext context;
    BenchmarkCampaignMapCharacterization characterization;
};

/** @brief Returns a stable @c map_id string for @p spec and @p index. @ingroup hbrick_bench */
[[nodiscard]] std::string proceduralMapId(
    const ProceduralMazeSpec& spec,
    uint32_t index
);

/**
 * @brief Builds layout, directed graph, recipe, and characterization for @p spec.
 * @ingroup hbrick_bench
 */
[[nodiscard]] bool generateProceduralCampaignMap(
    const ProceduralMazeSpec& spec,
    uint32_t index,
    const BenchmarkCampaignPaths& paths,
    const std::string& campaign_id,
    GeneratedCampaignMap& generated,
    std::string& error_message
);

/**
 * @brief Computes characterization statistics for @p layout and @p graph.
 * @ingroup hbrick_bench
 */
[[nodiscard]] BenchmarkCampaignMapCharacterization characterizeCampaignMap(
    const MazeLayout& layout,
    const DirectedGridGraph& graph,
    const ProceduralMazeSpec& spec,
    uint64_t gallery_image_hash
);

/**
 * @brief Generates @p count procedural maps and appends manifest rows (no benchmarks).
 * @ingroup hbrick_bench
 */
[[nodiscard]] bool generateProceduralCampaignMaps(
    const BenchmarkCampaignPaths& paths,
    const BenchmarkCampaignMetadata& metadata,
    const ProceduralMazeSpec& spec,
    uint32_t count,
    std::string& error_message
);

/** @brief Orientation mode label for CSV output. @ingroup hbrick_bench */
[[nodiscard]] const char* gridEdgeConversionModeLabel(
    GridEdgeConversionMode mode
) noexcept;

/**
 * @brief Parameters for importing one MovingAI map into a campaign.
 * @ingroup hbrick_bench
 */
struct MovingAiCampaignMapSpec {
    std::string set_name;
    std::string map_name;
    std::filesystem::path datasets_root;
    MovingAiPassabilityPolicy policy = MovingAiPassabilityPolicy::GroundOnly;
    GridEdgeConversionMode orientation_mode = GridEdgeConversionMode::RandomAsymmetric;
    uint64_t orientation_seed = 0U;
    RandomAsymmetricParams asymmetric_params{};
};

/** @brief Returns a stable @c map_id for a MovingAI map. @ingroup hbrick_bench */
[[nodiscard]] std::string movingAiCampaignMapId(
    const std::string& set_name,
    const std::string& map_name
);

/** @brief Builds a spec with catalog-default orientation parameters. @ingroup hbrick_bench */
[[nodiscard]] MovingAiCampaignMapSpec movingAiCampaignMapSpecForEntry(
    const std::string& set_name,
    const std::string& map_name,
    const std::filesystem::path& datasets_root
);

/** @brief Default extracted MovingAI root relative to the source tree. @ingroup hbrick_bench */
[[nodiscard]] std::filesystem::path defaultMovingAiDatasetsRoot();

/**
 * @brief Builds layout, graph, recipe, and characterization for one MovingAI map.
 * @ingroup hbrick_bench
 */
[[nodiscard]] bool generateMovingAiCampaignMap(
    const MovingAiCampaignMapSpec& spec,
    const BenchmarkCampaignPaths& paths,
    const std::string& campaign_id,
    GeneratedCampaignMap& generated,
    std::string& error_message
);

/**
 * @brief Imports @p maps into the campaign manifest (no benchmarks).
 * @ingroup hbrick_bench
 */
[[nodiscard]] bool importMovingAiCampaignMaps(
    const BenchmarkCampaignPaths& paths,
    const BenchmarkCampaignMetadata& metadata,
    const std::filesystem::path& datasets_root,
    std::span<const MovingAiCampaignMapSpec> maps,
    std::string& error_message
);

/**
 * @brief Builds smoke-catalog specs (one smallest map per set) under @p datasets_root.
 * @ingroup hbrick_bench
 */
[[nodiscard]] std::vector<MovingAiCampaignMapSpec> movingAiSmokeCatalogSpecs(
    const std::filesystem::path& datasets_root
);

}  // namespace hbrick
