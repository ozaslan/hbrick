/**
 * @file tile_decomposition.hpp
 * @ingroup hbrick_tile
 * @brief Non-overlapping rectangular decomposition of a fine grid into tile slots.
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/grid/grid_dimensions.hpp"
#include "hbrick/tile/tile_size.hpp"
#include "hbrick/tile/tile_slot.hpp"

namespace hbrick {

/**
 * @brief Non-overlapping partition of a rectangular map into tile slots.
 * @ingroup hbrick_tile
 *
 * Nominal stride is @ref TileSize::width by @ref TileSize::height with no overlap.
 * Remainder strips narrower than two cells along an axis are merged into the
 * preceding slot on that axis so every stored slot has extent >= 2 per dimension.
 */
class TileDecomposition {
public:
    /**
     * @brief Builds a decomposition for a @p map_width by @p map_height fine grid.
     * @ingroup hbrick_tile
     *
     * @param map_width Map width in cells.
     * @param map_height Map height in cells.
     * @param nominal Nominal tile size (must satisfy @ref TileSize::isValid).
     * @return Decomposition covering the full map.
     * @throws std::invalid_argument when @p nominal is invalid or the map is too small.
     */
    [[nodiscard]] static TileDecomposition decompose(
        uint32_t map_width,
        uint32_t map_height,
        TileSize nominal
    );

    /** @brief Returns map width in cells. @ingroup hbrick_tile */
    [[nodiscard]] uint32_t mapWidth() const noexcept { return map_width_; }
    /** @brief Returns map height in cells. @ingroup hbrick_tile */
    [[nodiscard]] uint32_t mapHeight() const noexcept { return map_height_; }
    /** @brief Returns the nominal tile size used to build this decomposition. @ingroup hbrick_tile */
    [[nodiscard]] TileSize nominalSize() const noexcept { return nominal_; }

    /** @brief Number of slot columns. @ingroup hbrick_tile */
    [[nodiscard]] uint32_t numSlotsI() const noexcept {
        return static_cast<uint32_t>(axis_i_.origins.size());
    }
    /** @brief Number of slot rows. @ingroup hbrick_tile */
    [[nodiscard]] uint32_t numSlotsJ() const noexcept {
        return static_cast<uint32_t>(axis_j_.origins.size());
    }
    /** @brief Total number of tile slots. @ingroup hbrick_tile */
    [[nodiscard]] uint32_t numSlots() const noexcept {
        return static_cast<uint32_t>(slots_.size());
    }

    /**
     * @brief Returns the slot at (@p tile_i, @p tile_j).
     * @ingroup hbrick_tile
     */
    [[nodiscard]] const TileSlot& slot(uint32_t tile_i, uint32_t tile_j) const noexcept;

    /**
     * @brief Returns the slot with linear @p index (row-major over slot grid).
     * @ingroup hbrick_tile
     */
    [[nodiscard]] const TileSlot& slotByIndex(uint32_t index) const noexcept;

    /** @brief Read-only view of all slots in row-major order. @ingroup hbrick_tile */
    [[nodiscard]] std::span<const TileSlot> slots() const noexcept {
        return slots_;
    }

    /**
     * @brief Returns whether @p coord lies inside the map bounds.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] bool contains(GridCoord coord) const noexcept;

    /**
     * @brief Locates the slot containing @p coord.
     * @ingroup hbrick_tile
     *
     * @return @c true and writes @p out_slot when @p coord is in bounds.
     */
    [[nodiscard]] bool slotAt(
        GridCoord coord,
        TileSlotId& out_slot
    ) const noexcept;

    /**
     * @brief Returns the linear slot index for @p coord.
     * @ingroup hbrick_tile
     *
     * @return Slot index, or @c UINT32_MAX when @p coord is out of bounds.
     */
    [[nodiscard]] uint32_t slotIndexAt(GridCoord coord) const noexcept;

private:
    struct AxisPartition {
        std::vector<uint32_t> origins;
        std::vector<uint32_t> extents;
    };

    static AxisPartition partitionAxis(uint32_t map_len, uint32_t nominal_len);

    void buildSlots();

    uint32_t map_width_ = 0U;
    uint32_t map_height_ = 0U;
    TileSize nominal_{};
    AxisPartition axis_i_{};
    AxisPartition axis_j_{};
    std::vector<TileSlot> slots_{};
};

}  // namespace hbrick
