/**
 * @file tile_port.hpp
 * @ingroup hbrick_tile
 * @brief Boundary-port geometry helpers for tile slots.
 */

#pragma once

#include <cstdint>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/tile/tile_slot.hpp"

namespace hbrick {

/**
 * @brief Cardinal side of a tile bbox used for canonical port ordering.
 * @ingroup hbrick_tile
 *
 * Corners are assigned to @c North first, then @c East when both apply.
 */
enum class PortSide : uint8_t {
    /** @brief Northern boundary (minimum @c y). @ingroup hbrick_tile */
    North = 0,
    /** @brief Eastern boundary (maximum @c x - 1 edge). @ingroup hbrick_tile */
    East = 1,
    /** @brief Southern boundary (maximum @c y - 1 edge). @ingroup hbrick_tile */
    South = 2,
    /** @brief Western boundary (minimum @c x). @ingroup hbrick_tile */
    West = 3,
};

/**
 * @brief Returns whether @p coord lies on the perimeter of @p slot's bbox.
 * @ingroup hbrick_tile
 */
[[nodiscard]] constexpr bool isOnTilePerimeter(
    GridCoord coord,
    const TileSlot& slot
) noexcept {
    if (!slot.contains(coord)) {
        return false;
    }

    const bool on_north = coord.y == slot.origin.y;
    const bool on_south = coord.y + 1U == slot.maxY();
    const bool on_west = coord.x == slot.origin.x;
    const bool on_east = coord.x + 1U == slot.maxX();
    return on_north || on_south || on_west || on_east;
}

/**
 * @brief Returns the canonical port side for @p coord on @p slot's perimeter.
 * @ingroup hbrick_tile
 *
 * Corner precedence: @c North, then @c East, then @c South, then @c West.
 */
[[nodiscard]] constexpr PortSide portSideForCoord(
    GridCoord coord,
    const TileSlot& slot
) noexcept {
    const bool on_north = coord.y == slot.origin.y;
    const bool on_east = coord.x + 1U == slot.maxX();
    const bool on_south = coord.y + 1U == slot.maxY();
    const bool on_west = coord.x == slot.origin.x;

    if (on_north) {
        return PortSide::North;
    }
    if (on_east) {
        return PortSide::East;
    }
    if (on_south) {
        return PortSide::South;
    }
    if (on_west) {
        return PortSide::West;
    }
    return PortSide::North;
}

}  // namespace hbrick
