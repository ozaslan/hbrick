/**
 * @file external_boundary_ports.hpp
 * @ingroup hbrick_tile
 * @brief Fine-grid boundary port enumeration for an axis-aligned region bbox.
 */

#pragma once

#include <vector>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/tile/tile_slot.hpp"

namespace hbrick {

/**
 * @brief Returns perimeter cells of @p region_bbox in canonical N→E→S→W order.
 * @ingroup hbrick_tile
 */
[[nodiscard]] std::vector<GridCoord> externalBoundaryPorts(const TileSlot& region_bbox);

}  // namespace hbrick
