/**
 * @file tile_size.hpp
 * @ingroup hbrick_tile
 * @brief Nominal width and height of a rectangular cell tile at hierarchy level 0.
 */

#pragma once

#include <cstdint>

namespace hbrick {

/**
 * @brief Nominal footprint of a base (level-0) cell tile.
 * @ingroup hbrick_tile
 *
 * Both dimensions must be at least @c 2 so the tile has a meaningful boundary
 * strip along each axis after clipping to the map.
 */
struct TileSize {
    /** @brief Tile width in fine-grid cells (columns). @ingroup hbrick_tile */
    uint32_t width = 0;
    /** @brief Tile height in fine-grid cells (rows). @ingroup hbrick_tile */
    uint32_t height = 0;

    /** @brief Default-constructs a zero size (invalid). @ingroup hbrick_tile */
    constexpr TileSize() noexcept = default;

    /**
     * @brief Constructs a tile size from explicit dimensions.
     * @ingroup hbrick_tile
     */
    constexpr TileSize(uint32_t width_value, uint32_t height_value) noexcept
        : width(width_value), height(height_value) {}

    /**
     * @brief Returns whether both dimensions are at least @c 2.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return width >= 2U && height >= 2U;
    }

    /** @brief Equality on both dimensions. @ingroup hbrick_tile */
    friend constexpr bool operator==(TileSize lhs, TileSize rhs) noexcept {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    /** @brief Inequality on both dimensions. @ingroup hbrick_tile */
    friend constexpr bool operator!=(TileSize lhs, TileSize rhs) noexcept {
        return !(lhs == rhs);
    }
};

}  // namespace hbrick
