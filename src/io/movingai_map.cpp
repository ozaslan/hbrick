#include "hbrick/io/movingai_map.hpp"

#include <stdexcept>
#include <utility>

namespace hbrick {

MovingAiTerrain movingAiTerrainFromChar(const char cell) noexcept {
    switch (cell) {
        case '.':
        case 'G':
            return MovingAiTerrain::Ground;
        case 'T':
            return MovingAiTerrain::Tree;
        case 'S':
            return MovingAiTerrain::Swamp;
        case 'W':
            return MovingAiTerrain::Water;
        case '@':
        case 'O':
            return MovingAiTerrain::OutOfBounds;
        default:
            if (cell >= 'A' && cell <= 'Z') {
                return MovingAiTerrain::WeightedTerrain;
            }
            return MovingAiTerrain::Unknown;
    }
}

const char* movingAiTerrainName(const MovingAiTerrain terrain) noexcept {
    switch (terrain) {
        case MovingAiTerrain::Ground:
            return "ground";
        case MovingAiTerrain::Tree:
            return "trees (impassable)";
        case MovingAiTerrain::Swamp:
            return "swamp (passable from regular terrain)";
        case MovingAiTerrain::Water:
            return "water (traversable, not passable from terrain)";
        case MovingAiTerrain::OutOfBounds:
            return "out of bounds";
        case MovingAiTerrain::WeightedTerrain:
            return "weighted terrain class";
        case MovingAiTerrain::Unknown:
        default:
            return "unknown";
    }
}

bool isMovingAiCellPassable(
    const char cell,
    const MovingAiPassabilityPolicy policy
) noexcept {
    const MovingAiTerrain terrain = movingAiTerrainFromChar(cell);
    switch (terrain) {
        case MovingAiTerrain::Ground:
            return true;
        case MovingAiTerrain::Swamp:
            return policy != MovingAiPassabilityPolicy::GroundOnly;
        case MovingAiTerrain::Water:
            return policy == MovingAiPassabilityPolicy::AllTraversable
                || policy == MovingAiPassabilityPolicy::AnyTerrainLetter;
        case MovingAiTerrain::WeightedTerrain:
            return policy == MovingAiPassabilityPolicy::AnyTerrainLetter;
        case MovingAiTerrain::Tree:
        case MovingAiTerrain::OutOfBounds:
        case MovingAiTerrain::Unknown:
        default:
            return false;
    }
}

MovingAiMap::MovingAiMap(
    const GridDimensions dimensions,
    std::string type_name,
    std::vector<char> cells
)
    : dimensions_(dimensions),
      type_name_(std::move(type_name)),
      cells_(std::move(cells)) {
    uint32_t expected_cells = 0U;
    if (!dimensions_.tryNumCells(expected_cells)
        || cells_.size() != static_cast<std::size_t>(expected_cells)) {
        throw std::invalid_argument(
            "MovingAiMap: cells size must equal dimensions.numCells()"
        );
    }
}

char MovingAiMap::cellAt(const GridCoord coord) const noexcept {
    if (!dimensions_.contains(coord)) {
        return '@';
    }
    const std::size_t index =
        static_cast<std::size_t>(coord.y) * dimensions_.width
        + static_cast<std::size_t>(coord.x);
    return cells_[index];
}

MovingAiTerrain MovingAiMap::terrainAt(const GridCoord coord) const noexcept {
    if (!dimensions_.contains(coord)) {
        return MovingAiTerrain::OutOfBounds;
    }
    return movingAiTerrainFromChar(cellAt(coord));
}

MazeLayout MovingAiMap::toMazeLayout(const MovingAiPassabilityPolicy policy) const {
    MazeLayout layout(dimensions_.width, dimensions_.height, false);
    for (uint32_t y = 0; y < dimensions_.height; ++y) {
        for (uint32_t x = 0; x < dimensions_.width; ++x) {
            const GridCoord coord{x, y};
            if (isMovingAiCellPassable(cellAt(coord), policy)) {
                layout.setPassable(coord, true);
            }
        }
    }
    return layout;
}

}  // namespace hbrick
