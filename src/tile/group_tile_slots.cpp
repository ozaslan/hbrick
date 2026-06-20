#include "hbrick/tile/group_tile_slots.hpp"

#include <stdexcept>

namespace hbrick {

namespace {

struct AxisGroupPartition {
    std::vector<uint32_t> origins;
    std::vector<uint32_t> extents;
};

[[nodiscard]] AxisGroupPartition partitionSlotAxis(
    const uint32_t num_slots,
    const uint32_t group_len
) {
    AxisGroupPartition partition;
    if (num_slots == 0U || group_len == 0U) {
        return partition;
    }

    uint32_t pos = 0U;
    while (pos < num_slots) {
        const uint32_t remaining = num_slots - pos;
        if (remaining <= group_len) {
            partition.origins.push_back(pos);
            partition.extents.push_back(remaining);
            break;
        }

        partition.origins.push_back(pos);
        partition.extents.push_back(group_len);
        pos += group_len;
    }

    return partition;
}

[[nodiscard]] std::vector<SlotGroup> groupSlotGrid(
    const uint32_t num_slots_i,
    const uint32_t num_slots_j,
    const GroupSize group_size
) {
    const AxisGroupPartition axis_i = partitionSlotAxis(num_slots_i, group_size.group_w);
    const AxisGroupPartition axis_j = partitionSlotAxis(num_slots_j, group_size.group_h);

    std::vector<SlotGroup> groups;
    groups.reserve(
        static_cast<std::size_t>(axis_i.origins.size()) *
        static_cast<std::size_t>(axis_j.origins.size())
    );

    for (uint32_t region_j = 0U; region_j < static_cast<uint32_t>(axis_j.origins.size());
         ++region_j) {
        for (uint32_t region_i = 0U; region_i < static_cast<uint32_t>(axis_i.origins.size());
             ++region_i) {
            SlotGroup group{};
            group.region_i = region_i;
            group.region_j = region_j;
            group.child_slot_i_begin = axis_i.origins[region_i];
            group.child_slot_j_begin = axis_j.origins[region_j];
            group.child_slot_i_end = group.child_slot_i_begin + axis_i.extents[region_i];
            group.child_slot_j_end = group.child_slot_j_begin + axis_j.extents[region_j];
            groups.push_back(group);
        }
    }

    return groups;
}

}  // namespace

std::vector<SlotGroup> groupTileSlots(
    const TileDecomposition& decomposition,
    const GroupSize group_size
) {
    if (!group_size.isValid()) {
        throw std::invalid_argument("groupTileSlots: group size must be at least 1x1");
    }

    return groupSlotGrid(decomposition.numSlotsI(), decomposition.numSlotsJ(), group_size);
}

std::vector<SlotGroup> groupTileSlots(
    const uint32_t num_slots_i,
    const uint32_t num_slots_j,
    const GroupSize group_size
) {
    if (!group_size.isValid()) {
        throw std::invalid_argument("groupTileSlots: group size must be at least 1x1");
    }

    return groupSlotGrid(num_slots_i, num_slots_j, group_size);
}

}  // namespace hbrick
