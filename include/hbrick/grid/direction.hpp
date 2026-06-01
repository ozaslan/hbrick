#pragma once

#include <cstdint>

namespace hbrick {

enum class Direction : uint8_t {
    East = 0,
    South = 1,
    West = 2,
    North = 3
};

[[nodiscard]] constexpr int32_t directionDeltaX(Direction direction) noexcept {
    switch (direction) {
        case Direction::East:
            return 1;
        case Direction::South:
            return 0;
        case Direction::West:
            return -1;
        case Direction::North:
            return 0;
    }
    return 0;
}

[[nodiscard]] constexpr int32_t directionDeltaY(Direction direction) noexcept {
    switch (direction) {
        case Direction::East:
            return 0;
        case Direction::South:
            return 1;
        case Direction::West:
            return 0;
        case Direction::North:
            return -1;
    }
    return 0;
}

}  // namespace hbrick
