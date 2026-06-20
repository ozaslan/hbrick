#include "hbrick/tile/tile_decomposition.hpp"

#include <limits>
#include <stdexcept>

namespace hbrick {

TileDecomposition::AxisPartition TileDecomposition::partitionAxis(
    uint32_t map_len,
    uint32_t nominal_len
) {
    AxisPartition partition;
    if (map_len == 0U || nominal_len == 0U) {
        return partition;
    }

    uint32_t pos = 0U;
    while (pos < map_len) {
        const uint32_t remaining = map_len - pos;
        if (remaining <= nominal_len) {
            if (remaining < 2U && !partition.origins.empty()) {
                partition.extents.back() += remaining;
            } else {
                partition.origins.push_back(pos);
                partition.extents.push_back(remaining);
            }
            break;
        }

        partition.origins.push_back(pos);
        partition.extents.push_back(nominal_len);
        pos += nominal_len;
    }

    return partition;
}

TileDecomposition TileDecomposition::decompose(
    uint32_t map_width,
    uint32_t map_height,
    TileSize nominal
) {
    if (!nominal.isValid()) {
        throw std::invalid_argument("TileDecomposition::decompose: nominal tile size must be at least 2x2");
    }
    if (map_width < 2U || map_height < 2U) {
        throw std::invalid_argument("TileDecomposition::decompose: map dimensions must be at least 2x2");
    }

    TileDecomposition decomposition;
    decomposition.map_width_ = map_width;
    decomposition.map_height_ = map_height;
    decomposition.nominal_ = nominal;
    decomposition.axis_i_ = partitionAxis(map_width, nominal.width);
    decomposition.axis_j_ = partitionAxis(map_height, nominal.height);

    if (decomposition.axis_i_.origins.empty() || decomposition.axis_j_.origins.empty()) {
        throw std::invalid_argument("TileDecomposition::decompose: failed to partition map axes");
    }

    decomposition.buildSlots();
    return decomposition;
}

void TileDecomposition::buildSlots() {
    slots_.clear();
    slots_.reserve(static_cast<std::size_t>(numSlotsI()) * static_cast<std::size_t>(numSlotsJ()));

    for (uint32_t tile_j = 0U; tile_j < numSlotsJ(); ++tile_j) {
        for (uint32_t tile_i = 0U; tile_i < numSlotsI(); ++tile_i) {
            TileSlot slot{};
            slot.tile_i = tile_i;
            slot.tile_j = tile_j;
            slot.origin = GridCoord{axis_i_.origins[tile_i], axis_j_.origins[tile_j]};
            slot.extent = GridDimensions{axis_i_.extents[tile_i], axis_j_.extents[tile_j]};
            slots_.push_back(slot);
        }
    }
}

const TileSlot& TileDecomposition::slot(uint32_t tile_i, uint32_t tile_j) const noexcept {
    const uint32_t index = tile_j * numSlotsI() + tile_i;
    return slots_[index];
}

const TileSlot& TileDecomposition::slotByIndex(uint32_t index) const noexcept {
    return slots_[index];
}

bool TileDecomposition::contains(GridCoord coord) const noexcept {
    return coord.x < map_width_ && coord.y < map_height_;
}

bool TileDecomposition::slotAt(GridCoord coord, TileSlotId& out_slot) const noexcept {
    if (!contains(coord)) {
        return false;
    }

    for (uint32_t tile_j = 0U; tile_j < numSlotsJ(); ++tile_j) {
        for (uint32_t tile_i = 0U; tile_i < numSlotsI(); ++tile_i) {
            const TileSlot& candidate = slot(tile_i, tile_j);
            if (candidate.contains(coord)) {
                out_slot.index = tile_j * numSlotsI() + tile_i;
                out_slot.tile_i = tile_i;
                out_slot.tile_j = tile_j;
                return true;
            }
        }
    }

    return false;
}

uint32_t TileDecomposition::slotIndexAt(GridCoord coord) const noexcept {
    TileSlotId slot_id{};
    if (!slotAt(coord, slot_id)) {
        return std::numeric_limits<uint32_t>::max();
    }
    return slot_id.index;
}

}  // namespace hbrick
