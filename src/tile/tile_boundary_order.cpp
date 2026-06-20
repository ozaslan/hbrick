#include "hbrick/tile/tile_boundary_order.hpp"

namespace hbrick {

std::vector<GridCoord> canonicalBoundaryCoords(const TileSlot& slot) {
    std::vector<GridCoord> coords;
    const uint32_t max_x = slot.maxX();
    const uint32_t max_y = slot.maxY();

    if (slot.extent.width == 0U || slot.extent.height == 0U) {
        return coords;
    }

    coords.reserve(2U * (slot.extent.width + slot.extent.height));

    for (uint32_t x = slot.origin.x; x < max_x; ++x) {
        coords.push_back(GridCoord{x, slot.origin.y});
    }

    if (slot.extent.height > 1U) {
        for (uint32_t y = slot.origin.y + 1U; y < max_y; ++y) {
            coords.push_back(GridCoord{max_x - 1U, y});
        }
    }

    if (slot.extent.height > 1U) {
        for (uint32_t x = slot.origin.x; x + 1U < max_x; ++x) {
            coords.push_back(GridCoord{x, max_y - 1U});
        }
    }

    if (slot.extent.width > 1U && slot.extent.height > 2U) {
        for (uint32_t y = slot.origin.y + 1U; y + 1U < max_y; ++y) {
            coords.push_back(GridCoord{slot.origin.x, y});
        }
    }

    return coords;
}

}  // namespace hbrick
