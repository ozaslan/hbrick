/**
 * @file tile_port_record.hpp
 * @ingroup hbrick_tile
 * @brief One boundary port in a base tile summary.
 */

#pragma once

#include <cstdint>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/tile/tile_port.hpp"

namespace hbrick {

/**
 * @brief One passable boundary cell exported as a port in a @ref BaseTileSummary.
 * @ingroup hbrick_tile
 */
struct TilePort {
    /** @brief Fine-grid coordinate of this port cell. @ingroup hbrick_tile */
    GridCoord coord{};
    /** @brief Index into the tile's local vertex / closure ordering. @ingroup hbrick_tile */
    uint32_t local_index = 0U;
    /** @brief Canonical side used for deterministic port ordering. @ingroup hbrick_tile */
    PortSide side = PortSide::North;
};

}  // namespace hbrick
