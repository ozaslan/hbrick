/**
 * @file region_node.hpp
 * @ingroup hbrick_tile
 * @brief One node in the H-BRICK region hierarchy tree.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/grid/grid_dimensions.hpp"

namespace hbrick {

/**
 * @brief Identifies one region node at a hierarchy level.
 * @ingroup hbrick_tile
 */
struct RegionNodeId {
    /** @brief Hierarchy depth (@c 0 = base tile slots). @ingroup hbrick_tile */
    uint32_t level = 0U;
    /** @brief Row-major index among nodes at @ref level. @ingroup hbrick_tile */
    uint32_t index = 0U;

    friend constexpr bool operator==(RegionNodeId lhs, RegionNodeId rhs) noexcept {
        return lhs.level == rhs.level && lhs.index == rhs.index;
    }

    friend constexpr bool operator!=(RegionNodeId lhs, RegionNodeId rhs) noexcept {
        return !(lhs == rhs);
    }
};

/**
 * @brief One grouped region in the H-BRICK hierarchy.
 * @ingroup hbrick_tile
 */
struct RegionNode {
    /** @brief This node's identity. @ingroup hbrick_tile */
    RegionNodeId id{};
    /** @brief Parent region when @ref has_parent is @c true. @ingroup hbrick_tile */
    RegionNodeId parent{};
    /** @brief @c true when @ref parent is valid. @ingroup hbrick_tile */
    bool has_parent = false;
    /** @brief Direct child regions at level @c id.level - 1. @ingroup hbrick_tile */
    std::vector<RegionNodeId> children{};

    /** @brief Region column in the slot grid at this level. @ingroup hbrick_tile */
    uint32_t region_i = 0U;
    /** @brief Region row in the slot grid at this level. @ingroup hbrick_tile */
    uint32_t region_j = 0U;
    /** @brief Fine-grid origin of the region bbox (inclusive). @ingroup hbrick_tile */
    GridCoord origin{};
    /** @brief Fine-grid extent of the region bbox. @ingroup hbrick_tile */
    GridDimensions extent{};
};

}  // namespace hbrick
