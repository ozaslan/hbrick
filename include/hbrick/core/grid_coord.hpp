/**
 * @file grid_coord.hpp
 * @ingroup hbrick_core
 * @brief Two-dimensional grid coordinates for maze and grid-graph APIs.
 */

#pragma once

#include <cstdint>

namespace hbrick {

/**
 * @brief Integer coordinate of a cell within a rectangular grid.
 * @ingroup hbrick_core
 *
 * Coordinates use zero-based indexing with @c x increasing to the east and
 * @c y increasing to the south, matching the layout used by @ref hbrick::PassableGrid
 * and @ref hbrick::DirectedGridGraph.
 */
struct GridCoord {
    /** @brief Column index (east-west axis). @ingroup hbrick_core */
    uint32_t x = 0;
    /** @brief Row index (north-south axis). @ingroup hbrick_core */
    uint32_t y = 0;

    /** @brief Default-constructs the origin coordinate @c (0, 0). @ingroup hbrick_core */
    constexpr GridCoord() noexcept = default;

    /**
     * @brief Constructs a coordinate from explicit indices.
     * @ingroup hbrick_core
     *
     * @param x_value Column index.
     * @param y_value Row index.
     */
    constexpr GridCoord(uint32_t x_value, uint32_t y_value) noexcept
        : x(x_value), y(y_value) {}

    /**
     * @brief Maps this coordinate to a row-major linear cell index.
     * @ingroup hbrick_core
     *
     * @param width Grid width in cells; must be non-zero for meaningful results.
     * @return @c y * width + x.
     */
    [[nodiscard]] constexpr uint32_t linearIndex(uint32_t width) const noexcept {
        return y * width + x;
    }

    /** @brief Equality comparison on both axes. @ingroup hbrick_core */
    friend constexpr bool operator==(GridCoord lhs, GridCoord rhs) noexcept {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }

    /** @brief Inequality comparison on both axes. @ingroup hbrick_core */
    friend constexpr bool operator!=(GridCoord lhs, GridCoord rhs) noexcept {
        return !(lhs == rhs);
    }
};

}  // namespace hbrick
