#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/core/vertex_id.hpp"
#include "hbrick/grid/direction.hpp"
#include "hbrick/grid/grid_dimensions.hpp"

namespace hbrick {

class PassableGrid {
public:
    PassableGrid(uint32_t width, uint32_t height, bool initially_passable = true);

    [[nodiscard]] uint32_t width() const noexcept { return dimensions_.width; }
    [[nodiscard]] uint32_t height() const noexcept { return dimensions_.height; }
    [[nodiscard]] uint32_t numVertices() const noexcept { return dimensions_.numCells(); }
    [[nodiscard]] GridDimensions dimensions() const noexcept { return dimensions_; }

    [[nodiscard]] bool contains(GridCoord coord) const noexcept {
        return dimensions_.contains(coord);
    }

    [[nodiscard]] bool isPassable(GridCoord coord) const noexcept;
    [[nodiscard]] bool isPassable(uint32_t x, uint32_t y) const noexcept;
    [[nodiscard]] bool isPassable(VertexId vertex) const noexcept;

    void setPassable(GridCoord coord, bool passable);
    void setPassable(uint32_t x, uint32_t y, bool passable);

    [[nodiscard]] VertexId vertexId(GridCoord coord) const noexcept;
    [[nodiscard]] GridCoord coordFromVertex(VertexId vertex) const noexcept;

    [[nodiscard]] bool tryNeighbor(
        GridCoord from,
        Direction direction,
        GridCoord& out
    ) const noexcept;

    [[nodiscard]] uint32_t passableCount() const noexcept;

    template <typename Callback>
    void forEachPassableAdjacentPairEastSouth(Callback&& callback) const {
        for (uint32_t y = 0; y < dimensions_.height; ++y) {
            for (uint32_t x = 0; x < dimensions_.width; ++x) {
                const GridCoord from{x, y};
                if (!isPassable(from)) {
                    continue;
                }

                const GridCoord east{x + 1U, y};
                if (contains(east) && isPassable(east)) {
                    callback(from, east, Direction::East);
                }

                const GridCoord south{x, y + 1U};
                if (contains(south) && isPassable(south)) {
                    callback(from, south, Direction::South);
                }
            }
        }
    }

private:
    [[nodiscard]] std::size_t cellIndex(GridCoord coord) const noexcept;

    GridDimensions dimensions_;
    std::vector<uint8_t> passable_;
};

}  // namespace hbrick
