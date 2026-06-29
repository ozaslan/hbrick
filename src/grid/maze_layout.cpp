#include "hbrick/grid/maze_layout.hpp"

#include <stdexcept>

namespace hbrick {

MazeLayout::MazeLayout(
    const uint32_t width,
    const uint32_t height,
    const bool initially_passable
)
    : dimensions_(width, height) {
    uint32_t num_cells = 0U;
    if (!dimensions_.tryNumCells(num_cells)) {
        throw std::invalid_argument("MazeLayout: width * height overflows uint32_t");
    }
    passable_.assign(num_cells, initially_passable ? uint8_t{1} : uint8_t{0});
}

bool MazeLayout::isPassable(const GridCoord coord) const noexcept {
    if (!contains(coord)) {
        return false;
    }
    return passable_[cellIndex(coord)] != 0;
}

bool MazeLayout::isPassable(const uint32_t x, const uint32_t y) const noexcept {
    return isPassable(GridCoord{x, y});
}

bool MazeLayout::isPassable(const VertexId vertex) const noexcept {
    if (!vertex.isValid() || vertex.value >= numVertices()) {
        return false;
    }
    return passable_[vertex.value] != 0;
}

void MazeLayout::setPassable(const GridCoord coord, const bool passable) {
    if (!contains(coord)) {
        return;
    }
    passable_[cellIndex(coord)] = passable ? uint8_t{1} : uint8_t{0};
}

void MazeLayout::setPassable(const uint32_t x, const uint32_t y, const bool passable) {
    setPassable(GridCoord{x, y}, passable);
}

VertexId MazeLayout::vertexId(const GridCoord coord) const noexcept {
    if (!contains(coord)) {
        return VertexId::invalid();
    }
    uint32_t linear_index = 0U;
    if (!coord.tryLinearIndex(dimensions_.width, linear_index)) {
        return VertexId::invalid();
    }
    return VertexId{linear_index};
}

GridCoord MazeLayout::coordFromVertex(const VertexId vertex) const noexcept {
    if (!vertex.isValid() || vertex.value >= numVertices() || dimensions_.width == 0) {
        return GridCoord{};
    }
    return coordFromLinearIndex(dimensions_.width, vertex.value);
}

bool MazeLayout::tryNeighbor(
    const GridCoord from,
    const Direction direction,
    GridCoord& out
) const noexcept {
    const int32_t next_x =
        static_cast<int32_t>(from.x) + directionDeltaX(direction);
    const int32_t next_y =
        static_cast<int32_t>(from.y) + directionDeltaY(direction);

    if (next_x < 0 || next_y < 0) {
        return false;
    }

    const GridCoord candidate{
        static_cast<uint32_t>(next_x),
        static_cast<uint32_t>(next_y),
    };

    if (!contains(candidate) || !isPassable(candidate)) {
        return false;
    }

    out = candidate;
    return true;
}

uint32_t MazeLayout::passableCount() const noexcept {
    uint32_t count = 0;
    for (const uint8_t cell_passable : passable_) {
        if (cell_passable != 0) {
            ++count;
        }
    }
    return count;
}

std::size_t MazeLayout::cellIndex(const GridCoord coord) const noexcept {
    return static_cast<std::size_t>(coord.y) * dimensions_.width
        + static_cast<std::size_t>(coord.x);
}

}  // namespace hbrick
