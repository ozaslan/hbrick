/**
 * @file maze_generator.hpp
 * @ingroup hbrick_test_support
 * @brief Deterministic maze fixtures for integration and regression tests.
 */

#pragma once

#include <cstdint>

#include "hbrick/grid/maze_layout.hpp"

namespace hbrick::test_support {

/**
 * @brief Parameters controlling logical maze size and random seed.
 * @ingroup hbrick_test_support
 *
 * The physical grid produced by @ref generatePerfectMaze is larger than the
 * logical room count because wall cells occupy even coordinates.
 */
struct MazeParams {
    /** @brief Number of room columns in the logical maze. @ingroup hbrick_test_support */
    uint32_t logical_width = 10U;
    /** @brief Number of room rows in the logical maze. @ingroup hbrick_test_support */
    uint32_t logical_height = 10U;
    /** @brief Seed for the deterministic carving algorithm. @ingroup hbrick_test_support */
    uint64_t seed = 0U;
};

/**
 * @brief Builds a perfect maze using recursive backtracking.
 * @ingroup hbrick_test_support
 *
 * Physical grid size is @c (2 * logical_width + 1) by @c (2 * logical_height + 1).
 * Odd coordinates are rooms; carved wall cells connect neighboring rooms.
 *
 * @param params Logical dimensions and carving seed.
 * @return Maze layout representing the carved maze.
 */
[[nodiscard]] MazeLayout generatePerfectMaze(const MazeParams& params);

/**
 * @brief Introduces extra openings into a perfect maze to create cycles.
 * @ingroup hbrick_test_support
 *
 * Starts from the maze that @ref generatePerfectMaze would produce for
 * @p params, then removes additional wall cells.
 *
 * @param params Logical dimensions and base maze seed.
 * @param opening_seed Seed controlling which extra walls are removed.
 * @param extra_openings Number of additional passages to carve.
 * @return Maze layout with cycles added beyond a perfect maze.
 */
[[nodiscard]] MazeLayout generateMazeWithExtraPassages(
    const MazeParams& params,
    uint64_t opening_seed,
    uint32_t extra_openings
);

}  // namespace hbrick::test_support
