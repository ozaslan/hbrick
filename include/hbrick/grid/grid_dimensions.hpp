/**
 * @file grid_dimensions.hpp
 * @ingroup hbrick_grid
 * @brief Width/height metadata and linear-index helpers for rectangular grids.
 */

#pragma once

#include <cstdint>
#include <limits>

#include "hbrick/core/grid_coord.hpp"

namespace hbrick {

/**
 * @brief Width and height of a rectangular cell grid.
 * @ingroup hbrick_grid
 *
 * Provides bounds checks and cell-count helpers shared by @ref hbrick::MazeLayout
 * and grid-to-graph conversion utilities.
 */
struct GridDimensions {
    /** @brief Number of columns (east-west extent). @ingroup hbrick_grid */
    uint32_t width = 0;
    /** @brief Number of rows (north-south extent). @ingroup hbrick_grid */
    uint32_t height = 0;

    /** @brief Default-constructs a zero-sized grid. @ingroup hbrick_grid */
    constexpr GridDimensions() noexcept = default;

    /**
     * @brief Constructs dimensions from explicit width and height.
     * @ingroup hbrick_grid
     *
     * @param width_value Column count.
     * @param height_value Row count.
     */
    constexpr GridDimensions(uint32_t width_value, uint32_t height_value) noexcept
        : width(width_value), height(height_value) {}

    /**
     * @brief Returns the total number of cells in the grid when representable.
     * @ingroup hbrick_grid
     *
     * @param out Receives @c width * height on success.
     * @return @c false when the product does not fit in @c uint32_t.
     */
    [[nodiscard]] constexpr bool tryNumCells(uint32_t& out) const noexcept {
        const uint64_t product =
            static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
        if (product > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
            return false;
        }
        out = static_cast<uint32_t>(product);
        return true;
    }

    /**
     * @brief Returns the total number of cells in the grid.
     * @ingroup hbrick_grid
     *
     * @pre @ref tryNumCells succeeds for these dimensions.
     * @return @c width * height.
     */
    [[nodiscard]] constexpr uint32_t numCells() const noexcept {
        const uint64_t product =
            static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
        return static_cast<uint32_t>(product);
    }

    /**
     * @brief Returns whether @p coord lies inside the grid bounds.
     * @ingroup hbrick_grid
     *
     * @param coord Coordinate to test.
     * @return @c true when @c coord.x < width and @c coord.y < height.
     */
    [[nodiscard]] constexpr bool contains(GridCoord coord) const noexcept {
        return coord.x < width && coord.y < height;
    }

    /**
     * @brief Returns whether both dimensions are strictly positive.
     * @ingroup hbrick_grid
     *
     * @return @c true when @ref width and @ref height are both greater than zero.
     */
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return width > 0 && height > 0;
    }

    /** @brief Compares width and height for equality. @ingroup hbrick_grid */
    friend constexpr bool operator==(GridDimensions lhs, GridDimensions rhs) noexcept {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    /** @brief Compares width and height for inequality. @ingroup hbrick_grid */
    friend constexpr bool operator!=(GridDimensions lhs, GridDimensions rhs) noexcept {
        return !(lhs == rhs);
    }
};

/**
 * @brief Converts a row-major linear index back to grid coordinates.
 * @ingroup hbrick_grid
 *
 * @param width Grid width in cells. When zero, returns the default origin.
 * @param linear_index Row-major index, typically @c y * width + x.
 * @return The corresponding @ref hbrick::GridCoord.
 */
[[nodiscard]] constexpr GridCoord coordFromLinearIndex(
    uint32_t width,
    uint32_t linear_index
) noexcept {
    if (width == 0) {
        return GridCoord{};
    }
    return GridCoord{linear_index % width, linear_index / width};
}

}  // namespace hbrick
