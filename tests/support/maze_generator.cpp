#include "maze_generator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <vector>

namespace hbrick::test_support {

namespace {

constexpr std::array<std::array<int32_t, 2>, 4> kDirections{{
    {{0, -1}},
    {{1, 0}},
    {{0, 1}},
    {{-1, 0}},
}};

[[nodiscard]] std::size_t logicalIndex(
    const uint32_t logical_width,
    const uint32_t x,
    const uint32_t y
) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(logical_width)
        + static_cast<std::size_t>(x);
}

void setPassableRoom(PassableGrid& grid, const uint32_t logical_x, const uint32_t logical_y) {
    grid.setPassable(2U * logical_x + 1U, 2U * logical_y + 1U, true);
}

void carveWallBetween(
    PassableGrid& grid,
    const uint32_t from_x,
    const uint32_t from_y,
    const uint32_t to_x,
    const uint32_t to_y
) {
    const uint32_t from_phys_x = 2U * from_x + 1U;
    const uint32_t from_phys_y = 2U * from_y + 1U;
    const uint32_t to_phys_x = 2U * to_x + 1U;
    const uint32_t to_phys_y = 2U * to_y + 1U;

    grid.setPassable(from_phys_x, from_phys_y, true);
    grid.setPassable(to_phys_x, to_phys_y, true);
    grid.setPassable(
        (from_phys_x + to_phys_x) / 2U,
        (from_phys_y + to_phys_y) / 2U,
        true
    );
}

void shuffleDirections(std::array<std::size_t, 4>& order, std::mt19937_64& rng) {
    for (std::size_t index = order.size(); index > 1U; --index) {
        const std::uniform_int_distribution<std::size_t> dist(0U, index - 1U);
        const std::size_t swap_index = dist(rng);
        std::swap(order[index - 1U], order[swap_index]);
    }
}

void carvePerfectMaze(
    PassableGrid& grid,
    const uint32_t logical_width,
    const uint32_t logical_height,
    std::mt19937_64& rng
) {
    std::vector<uint8_t> visited(
        static_cast<std::size_t>(logical_width) * static_cast<std::size_t>(logical_height),
        0U
    );
    std::vector<std::array<uint32_t, 2>> stack;
    stack.reserve(static_cast<std::size_t>(logical_width) * static_cast<std::size_t>(logical_height));

    const auto pushCell = [&](const uint32_t x, const uint32_t y) {
        visited[logicalIndex(logical_width, x, y)] = 1U;
        stack.push_back({x, y});
        setPassableRoom(grid, x, y);
    };

    pushCell(0U, 0U);

    while (!stack.empty()) {
        const std::array<uint32_t, 2> current = stack.back();
        const uint32_t current_x = current[0U];
        const uint32_t current_y = current[1U];

        std::array<std::size_t, 4> order{0U, 1U, 2U, 3U};
        shuffleDirections(order, rng);

        bool advanced = false;
        for (const std::size_t direction_index : order) {
            const int32_t delta_x = kDirections[direction_index][0U];
            const int32_t delta_y = kDirections[direction_index][1U];
            const int32_t next_x = static_cast<int32_t>(current_x) + delta_x;
            const int32_t next_y = static_cast<int32_t>(current_y) + delta_y;

            if (next_x < 0
                || next_y < 0
                || next_x >= static_cast<int32_t>(logical_width)
                || next_y >= static_cast<int32_t>(logical_height)) {
                continue;
            }

            const uint32_t neighbor_x = static_cast<uint32_t>(next_x);
            const uint32_t neighbor_y = static_cast<uint32_t>(next_y);
            if (visited[logicalIndex(logical_width, neighbor_x, neighbor_y)] != 0U) {
                continue;
            }

            carveWallBetween(grid, current_x, current_y, neighbor_x, neighbor_y);
            pushCell(neighbor_x, neighbor_y);
            advanced = true;
            break;
        }

        if (!advanced) {
            stack.pop_back();
        }
    }
}

void addExtraPassages(
    PassableGrid& grid,
    const uint32_t logical_width,
    const uint32_t logical_height,
    std::mt19937_64& rng,
    const uint32_t extra_openings
) {
    std::vector<std::array<uint32_t, 2>> candidate_walls;
    const uint32_t physical_width = grid.width();
    const uint32_t physical_height = grid.height();

    for (uint32_t y = 1U; y + 1U < physical_height; y += 2U) {
        for (uint32_t x = 2U; x + 1U < physical_width; x += 2U) {
            if (!grid.isPassable(x, y)) {
                candidate_walls.push_back({x, y});
            }
        }
    }

    for (uint32_t y = 2U; y + 1U < physical_height; y += 2U) {
        for (uint32_t x = 1U; x + 1U < physical_width; x += 2U) {
            if (!grid.isPassable(x, y)) {
                candidate_walls.push_back({x, y});
            }
        }
    }

    if (candidate_walls.empty()) {
        return;
    }

    std::shuffle(candidate_walls.begin(), candidate_walls.end(), rng);
    const uint32_t openings = std::min(extra_openings, static_cast<uint32_t>(candidate_walls.size()));
    for (uint32_t index = 0; index < openings; ++index) {
        const std::array<uint32_t, 2> wall = candidate_walls[static_cast<std::size_t>(index)];
        grid.setPassable(wall[0U], wall[1U], true);
    }

    static_cast<void>(logical_width);
    static_cast<void>(logical_height);
}

}  // namespace

PassableGrid generatePerfectMaze(const MazeParams& params) {
    const uint32_t physical_width = 2U * params.logical_width + 1U;
    const uint32_t physical_height = 2U * params.logical_height + 1U;
    PassableGrid grid(physical_width, physical_height, false);

    if (params.logical_width == 0U || params.logical_height == 0U) {
        return grid;
    }

    std::mt19937_64 rng{params.seed};
    carvePerfectMaze(grid, params.logical_width, params.logical_height, rng);
    return grid;
}

PassableGrid generateMazeWithExtraPassages(
    const MazeParams& params,
    const uint64_t opening_seed,
    const uint32_t extra_openings
) {
    PassableGrid grid = generatePerfectMaze(params);
    if (extra_openings == 0U) {
        return grid;
    }

    std::mt19937_64 opening_rng{opening_seed};
    addExtraPassages(
        grid,
        params.logical_width,
        params.logical_height,
        opening_rng,
        extra_openings
    );
    return grid;
}

}  // namespace hbrick::test_support
