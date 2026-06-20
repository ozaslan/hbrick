/**
 * @file hbrick_build_report.hpp
 * @ingroup hbrick_tile
 * @brief Progress and result statistics for incremental H-BRICK index construction.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hbrick/baselines/baseline_status.hpp"

namespace hbrick {

/** @brief Active phase of @ref HBrickIndexBuilder. @ingroup hbrick_tile */
enum class HBrickBuildStage : uint8_t {
    Idle = 0,
    BaseTiles = 1,
    FinalizeBrick = 2,
    Hierarchy = 3,
    SuperTiles = 4,
    Finished = 5,
};

/** @brief Live progress for one incremental build step. @ingroup hbrick_tile */
struct HBrickBuildProgress {
    HBrickBuildStage stage = HBrickBuildStage::Idle;
    uint64_t work_completed = 0U;
    uint64_t work_total = 0U;
    uint32_t current_level = 0U;
    uint32_t current_node_index = 0U;
    uint32_t current_base_tile_index = 0U;
    uint32_t num_base_tiles = 0U;
    uint32_t total_super_regions = 0U;
};

/** @brief Per super-level composition statistics. @ingroup hbrick_tile */
struct HBrickSuperLevelReport {
    uint32_t level = 0U;
    uint32_t num_regions = 0U;
    uint32_t num_completed = 0U;
    uint32_t num_skipped = 0U;
    uint64_t compose_nanoseconds = 0U;
};

/**
 * @brief Vertex counts for the query graph at one hierarchy level.
 * @ingroup hbrick_tile
 *
 * L0 is the single global flat port graph. L1+ counts one interface graph
 * (@c Gamma_U) per composed super region.
 */
struct HBrickLevelGraphStats {
    uint32_t level = 0U;
    /** @brief Number of graphs at this level (@c 1 for L0). @ingroup hbrick_tile */
    uint32_t num_graphs = 0U;
    /** @brief Sum of vertex counts across all graphs at this level. @ingroup hbrick_tile */
    uint64_t total_nodes = 0U;
    /** @brief Largest vertex count among graphs at this level. @ingroup hbrick_tile */
    uint32_t max_nodes = 0U;
};

/** @brief Final statistics from a completed @ref HBrickIndexBuilder run. @ingroup hbrick_tile */
struct HBrickBuildReport {
    bool valid = false;
    BaselineStatus status = BaselineStatus::NotRun;

    uint64_t total_nanoseconds = 0U;
    uint64_t base_tile_nanoseconds = 0U;
    /** @brief Time spent building L0 local cell closures (adjacency + Warshall). @ingroup hbrick_tile */
    uint64_t base_tile_closure_nanoseconds = 0U;
    uint64_t brick_finalize_nanoseconds = 0U;
    uint64_t hierarchy_nanoseconds = 0U;
    uint64_t super_compose_nanoseconds = 0U;

    uint32_t num_base_tiles = 0U;
    uint32_t num_base_completed = 0U;
    uint32_t num_base_skipped = 0U;
    /** @brief Completed base tiles with a non-empty L0 local closure matrix. @ingroup hbrick_tile */
    uint32_t num_base_with_closure = 0U;
    /** @brief Completed base tiles with no passable cells (no L0 closure). @ingroup hbrick_tile */
    uint32_t num_base_empty_impassable = 0U;
    uint32_t num_ports = 0U;
    uint32_t num_seams = 0U;
    uint32_t num_hierarchy_levels = 0U;
    uint64_t estimated_storage_bytes = 0U;
    uint64_t estimated_brick_storage_bytes = 0U;
    uint64_t estimated_hbrick_extra_storage_bytes = 0U;

    std::vector<HBrickLevelGraphStats> level_graph_stats;
    std::vector<HBrickSuperLevelReport> super_levels;
    std::string status_detail;
};

}  // namespace hbrick
