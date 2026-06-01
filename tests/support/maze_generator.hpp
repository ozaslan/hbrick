#pragma once

#include <cstdint>

#include "hbrick/grid/passable_grid.hpp"

namespace hbrick::test_support {

struct MazeParams {
    uint32_t logical_width = 10U;
    uint32_t logical_height = 10U;
    uint64_t seed = 0U;
};

// Builds a perfect maze with recursive backtracking.
// Physical grid size is (2 * logical_width + 1) x (2 * logical_height + 1).
// Odd coordinates are rooms; carved wall cells connect neighboring rooms.
[[nodiscard]] PassableGrid generatePerfectMaze(const MazeParams& params);

// Starts from a perfect maze and removes additional wall cells to create cycles.
[[nodiscard]] PassableGrid generateMazeWithExtraPassages(
    const MazeParams& params,
    uint64_t opening_seed,
    uint32_t extra_openings
);

}  // namespace hbrick::test_support
