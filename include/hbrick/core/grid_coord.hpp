#pragma once

#include <cstdint>

namespace hbrick {

struct GridCoord {
    uint32_t x = 0;
    uint32_t y = 0;

    constexpr GridCoord() noexcept = default;

    constexpr GridCoord(uint32_t x_value, uint32_t y_value) noexcept
        : x(x_value), y(y_value) {}

    [[nodiscard]] constexpr uint32_t linearIndex(uint32_t width) const noexcept {
        return y * width + x;
    }

    friend constexpr bool operator==(GridCoord lhs, GridCoord rhs) noexcept {
        return lhs.x == rhs.x && lhs.y == rhs.y;
    }

    friend constexpr bool operator!=(GridCoord lhs, GridCoord rhs) noexcept {
        return !(lhs == rhs);
    }
};

}  // namespace hbrick
