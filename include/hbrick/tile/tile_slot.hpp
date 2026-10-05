/**
 * @file tile_slot.hpp
 * @ingroup hbrick_tile
 * @brief One rectangular tile slot in a non-overlapping map decomposition.
 */

#pragma once

#include <cstdint>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/grid/grid_dimensions.hpp"

namespace hbrick {

/**
 * @brief Identifies one slot in a @ref hbrick::TileDecomposition grid.
 * @ingroup hbrick_tile
 */
struct TileSlotId {
    /** @brief Row-major linear index in the decomposition's slot array. @ingroup hbrick_tile */
    uint32_t index = 0U;
    /** @brief Slot column index. @ingroup hbrick_tile */
    uint32_t tile_i = 0U;
    /** @brief Slot row index. @ingroup hbrick_tile */
    uint32_t tile_j = 0U;

    /** @brief Equality on index and coordinates. @ingroup hbrick_tile */
    friend constexpr bool operator==(TileSlotId lhs, TileSlotId rhs) noexcept {
        return lhs.index == rhs.index && lhs.tile_i == rhs.tile_i && lhs.tile_j == rhs.tile_j;
    }

    /** @brief Inequality on index and coordinates. @ingroup hbrick_tile */
    friend constexpr bool operator!=(TileSlotId lhs, TileSlotId rhs) noexcept {
        return !(lhs == rhs);
    }
};

/**
 * @brief One axis-aligned tile slot with fine-grid origin and clipped extent.
 * @ingroup hbrick_tile
 */
struct TileSlot {
    /** @brief Slot column in the decomposition grid. @ingroup hbrick_tile */
    uint32_t tile_i = 0U;
    /** @brief Slot row in the decomposition grid. @ingroup hbrick_tile */
    uint32_t tile_j = 0U;
    /** @brief Fine-grid coordinate of the top-left cell (inclusive). @ingroup hbrick_tile */
    GridCoord origin{};
    /** @brief Clipped width and height in fine-grid cells (each >= 2). @ingroup hbrick_tile */
    GridDimensions extent{};

    /**
     * @brief Returns whether @p coord lies inside this slot's bbox.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] constexpr bool contains(GridCoord coord) const noexcept {
        if (coord.x < origin.x || coord.y < origin.y) {
            return false;
        }
        const uint32_t max_x = origin.x + extent.width;
        const uint32_t max_y = origin.y + extent.height;
        return coord.x < max_x && coord.y < max_y;
    }

    /**
     * @brief Returns the inclusive-exclusive x range [@c origin.x, @c origin.x + width).
     * @ingroup hbrick_tile
     */
    [[nodiscard]] constexpr uint32_t maxX() const noexcept {
        return origin.x + extent.width;
    }

    /**
     * @brief Returns the inclusive-exclusive y range [@c origin.y, @c origin.y + height).
     * @ingroup hbrick_tile
     */
    [[nodiscard]] constexpr uint32_t maxY() const noexcept {
        return origin.y + extent.height;
    }
};

}  // namespace hbrick
