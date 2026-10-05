/**
 * @file group_tile_slots.hpp
 * @ingroup hbrick_tile
 * @brief Groups base tile slots into parent regions for H-BRICK level >= 1.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/tile/group_size.hpp"
#include "hbrick/tile/tile_decomposition.hpp"

namespace hbrick {

/**
 * @brief One non-overlapping group of child tile slots at hierarchy level @c L-1.
 * @ingroup hbrick_tile
 */
struct SlotGroup {
    /** @brief Region column in the parent slot grid. @ingroup hbrick_tile */
    uint32_t region_i = 0U;
    /** @brief Region row in the parent slot grid. @ingroup hbrick_tile */
    uint32_t region_j = 0U;
    /** @brief Inclusive child slot column range [@ref child_slot_i_begin, @ref child_slot_i_end). @ingroup hbrick_tile */
    uint32_t child_slot_i_begin = 0U;
    /** @brief Inclusive child slot row range [@ref child_slot_j_begin, @ref child_slot_j_end). @ingroup hbrick_tile */
    uint32_t child_slot_j_begin = 0U;
    uint32_t child_slot_i_end = 0U;
    uint32_t child_slot_j_end = 0U;
};

/**
 * @brief Partitions @p decomposition's slot grid into @p group_size blocks.
 * @ingroup hbrick_tile
 *
 * Partial edge groups are allowed (unlike base tiles, groups may be 1 slot wide/tall).
 */
[[nodiscard]] std::vector<SlotGroup> groupTileSlots(
    const TileDecomposition& decomposition,
    GroupSize group_size
);

/**
 * @brief Partitions a @p num_slots_i by @p num_slots_j slot grid into groups.
 * @ingroup hbrick_tile
 */
[[nodiscard]] std::vector<SlotGroup> groupTileSlots(
    uint32_t num_slots_i,
    uint32_t num_slots_j,
    GroupSize group_size
);

}  // namespace hbrick
