#include "hbrick/tile/external_boundary_ports.hpp"

#include "hbrick/tile/tile_boundary_order.hpp"

namespace hbrick {

std::vector<GridCoord> externalBoundaryPorts(const TileSlot& region_bbox) {
    return canonicalBoundaryCoords(region_bbox);
}

}  // namespace hbrick
