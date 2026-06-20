/**
 * @file tile_boundary_order.hpp
 * @ingroup hbrick_tile
 * @brief Canonical perimeter traversal order for tile boundary ports.
 */

#pragma once

#include <vector>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/tile/tile_slot.hpp"

namespace hbrick {

/**
 * @brief Returns perimeter coordinates in N→E→S→W order with corners assigned once.
 * @ingroup hbrick_tile
 *
 * North row is fully enumerated first, then the east column (excluding the north
 * corner), then the south row (excluding the east corner), then the west column
 * (excluding north and south corners).
 */
[[nodiscard]] std::vector<GridCoord> canonicalBoundaryCoords(const TileSlot& slot);

}  // namespace hbrick
