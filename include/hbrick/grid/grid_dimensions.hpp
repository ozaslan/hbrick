#pragma once

#include <cstdint>

#include "hbrick/core/grid_coord.hpp"

namespace hbrick {

struct GridDimensions {
    uint32_t width = 0;
    uint32_t height = 0;

    constexpr GridDimensions() noexcept = default;

    constexpr GridDimensions(uint32_t width_value, uint32_t height_value) noexcept
        : width(width_value), height(height_value) {}

    [[nodiscard]] constexpr uint32_t numCells() const noexcept {
        return width * height;
    }

    [[nodiscard]] constexpr bool contains(GridCoord coord) const noexcept {
        return coord.x < width && coord.y < height;
    }

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return width > 0 && height > 0;
    }

    friend constexpr bool operator==(GridDimensions lhs, GridDimensions rhs) noexcept {
        return lhs.width == rhs.width && lhs.height == rhs.height;
    }

    friend constexpr bool operator!=(GridDimensions lhs, GridDimensions rhs) noexcept {
        return !(lhs == rhs);
    }
};

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
