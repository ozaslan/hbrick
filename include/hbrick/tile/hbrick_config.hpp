/**
 * @file hbrick_config.hpp
 * @ingroup hbrick_tile
 * @brief Configuration for hierarchical H-BRICK preprocessing.
 */

#pragma once

#include <cstdint>
#include <limits>

#include "hbrick/tile/group_size.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace hbrick {

/** @brief Build super-levels until a single root region remains. @ingroup hbrick_tile */
inline constexpr uint32_t kHBrickFullDepth = std::numeric_limits<uint32_t>::max();

/** @brief Disable retained-index memory-cap checks during preprocess. @ingroup hbrick_tile */
inline constexpr uint64_t kHBrickUnlimitedMemoryBytes =
    std::numeric_limits<uint64_t>::max();

/**
 * @brief Parameters controlling base tiling, grouping, and hierarchy depth.
 * @ingroup hbrick_tile
 */
struct HBrickConfig {
    /** @brief Fine cell tile size at hierarchy level @c 0. @ingroup hbrick_tile */
    TileSize base_tile_size{};
    /** @brief Child-slot grouping factor at levels @c >= 1. @ingroup hbrick_tile */
    GroupSize group_size{};
    /**
     * @brief Maximum hierarchy levels including level @c 0.
     * @ingroup hbrick_tile
     *
     * Use @ref kHBrickFullDepth to build until one root remains.
     */
    uint32_t max_depth = kHBrickFullDepth;
    /**
     * @brief Cap on retained index storage charged by @ref PreprocessMemoryLedger.
     * @ingroup hbrick_tile
     *
     * This is a retained-index budget, not a peak preprocessing RSS cap.
     * Temporary composition scratch (interface matrices, seam tables, gamma
     * indexes, SCC workspace) is not charged. Defaults to unlimited so a
     * default-constructed config produces a functional index.
     */
    uint64_t max_memory_bytes = kHBrickUnlimitedMemoryBytes;
    /**
     * @brief When true, omit the dense O(b^4) all-pairs intra-tile closure matrix
     *        from retained base tiles. Intra-tile queries use micro-BFS fallback.
     * @ingroup hbrick_tile
     */
    bool omit_local_closure = false;
};

}  // namespace hbrick
