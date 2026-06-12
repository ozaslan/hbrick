/**
 * @file direction.hpp
 * @ingroup hbrick_grid
 * @brief Cardinal directions and coordinate delta helpers for grid traversal.
 */

#pragma once

#include <cstdint>

namespace hbrick {

/**
 * @brief Cardinal direction on a rectangular grid.
 * @ingroup hbrick_grid
 *
 * Directions follow screen coordinates: east increases @c x, south increases
 * @c y. Used by @ref hbrick::MazeLayout::tryNeighbor and maze-generation utilities.
 */
enum class Direction : uint8_t {
    /** @brief One cell to the right (+x). @ingroup hbrick_grid */
    East = 0,
    /** @brief One cell downward (+y). @ingroup hbrick_grid */
    South = 1,
    /** @brief One cell to the left (-x). @ingroup hbrick_grid */
    West = 2,
    /** @brief One cell upward (-y). @ingroup hbrick_grid */
    North = 3
};

/**
 * @brief Returns the change in @c x when moving one step in @p direction.
 * @ingroup hbrick_grid
 *
 * @param direction Cardinal direction of movement.
 * @return @c -1, @c 0, or @c +1.
 */
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

/**
 * @brief Returns the change in @c y when moving one step in @p direction.
 * @ingroup hbrick_grid
 *
 * @param direction Cardinal direction of movement.
 * @return @c -1, @c 0, or @c +1.
 */
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
