/**
 * @file maze_layout.hpp
 * @ingroup hbrick_grid
 * @brief Mutable rectangular maze layout of passable and blocked cells.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/core/vertex_id.hpp"
#include "hbrick/grid/direction.hpp"
#include "hbrick/grid/grid_dimensions.hpp"

namespace hbrick {

/**
 * @brief Dense bitmap maze layout on a rectangular grid.
 * @ingroup hbrick_grid
 *
 * Each cell maps to a vertex index via row-major ordering. The layout is the
 * primary input for @ref hbrick::DirectedGridGraphBuilder and maze-based integration
 * tests. Hot neighbor queries are allocation-free.
 *
 * @see docs/representations.md for how MazeLayout relates to directed graphs.
 */
class MazeLayout {
public:
    /**
     * @brief Constructs a layout of the given size.
     * @ingroup hbrick_grid
     *
     * @param width Number of columns.
     * @param height Number of rows.
     * @param initially_passable When @c true, every cell starts passable.
     */
    MazeLayout(uint32_t width, uint32_t height, bool initially_passable = true);

    /** @brief Returns the grid width in cells. @return The grid width in cells. @ingroup hbrick_grid */
    [[nodiscard]] uint32_t width() const noexcept { return dimensions_.width; }
    /** @brief Returns the grid height in cells. @return The grid height in cells. @ingroup hbrick_grid */
    [[nodiscard]] uint32_t height() const noexcept { return dimensions_.height; }
    /** @brief Returns the total number of vertex slots (@c width * height). @return Total vertex count. @ingroup hbrick_grid */
    [[nodiscard]] uint32_t numVertices() const noexcept { return dimensions_.numCells(); }
    /** @brief Returns a copy of the stored width/height metadata. @return A copy of the stored width/height metadata. @ingroup hbrick_grid */
    [[nodiscard]] GridDimensions dimensions() const noexcept { return dimensions_; }

    /**
     * @brief Returns whether @p coord lies inside the grid bounds.
     * @ingroup hbrick_grid
     *
     * @param coord Coordinate to test.
     * @return True when the condition holds; otherwise false.
     */
    [[nodiscard]] bool contains(GridCoord coord) const noexcept {
        return dimensions_.contains(coord);
    }

    /**
     * @brief Returns whether the cell at @p coord is passable.
     * @ingroup hbrick_grid
     *
     * @param coord Cell coordinate. Must be contained by the grid.
     * @return True when the condition holds; otherwise false.
     */
    [[nodiscard]] bool isPassable(GridCoord coord) const noexcept;

    /**
     * @brief Returns whether the cell at (@p x, @p y) is passable.
     * @ingroup hbrick_grid
     *
     * @param x Column index.
     * @param y Row index.
     * @return True when the condition holds; otherwise false.
     */
    [[nodiscard]] bool isPassable(uint32_t x, uint32_t y) const noexcept;

    /**
     * @brief Returns whether the cell corresponding to @p vertex is passable.
     * @ingroup hbrick_grid
     *
     * @param vertex Vertex identifier whose raw index maps to a grid cell.
     * @return True when the condition holds; otherwise false.
     */
    [[nodiscard]] bool isPassable(VertexId vertex) const noexcept;

    /**
     * @brief Sets the passability of the cell at @p coord.
     * @ingroup hbrick_grid
     *
     * @param coord Target cell.
     * @param passable New passability flag.
     */
    void setPassable(GridCoord coord, bool passable);

    /**
     * @brief Sets the passability of the cell at (@p x, @p y).
     * @ingroup hbrick_grid
     *
     * @param x Column index.
     * @param y Row index.
     * @param passable New passability flag.
     */
    void setPassable(uint32_t x, uint32_t y, bool passable);

    /**
     * @brief Maps a grid coordinate to its vertex identifier.
     * @ingroup hbrick_grid
     *
     * @param coord Cell coordinate.
     * @return Vertex id equal to the row-major linear index.
     */
    [[nodiscard]] VertexId vertexId(GridCoord coord) const noexcept;

    /**
     * @brief Maps a vertex identifier back to grid coordinates.
     * @ingroup hbrick_grid
     *
     * @param vertex Vertex whose raw index encodes a linear cell index.
     * @return Corresponding grid coordinate.
     */
    [[nodiscard]] GridCoord coordFromVertex(VertexId vertex) const noexcept;

    /**
     * @brief Attempts to step one cell from @p from in @p direction.
     * @ingroup hbrick_grid
     *
     * Writes the destination coordinate to @p out only when the neighbor
     * exists, lies inside the grid, and is passable.
     *
     * @param from Starting cell.
     * @param direction Cardinal direction of movement.
     * @param out Receives the neighbor coordinate on success.
     * @return @c true when a passable neighbor was found.
     */
    [[nodiscard]] bool tryNeighbor(
        GridCoord from,
        Direction direction,
        GridCoord& out
    ) const noexcept;

    /**
     * @brief Counts how many cells are currently passable.
     * @ingroup hbrick_grid
     *
     * @return Number of passable cells in the grid.
     */
    [[nodiscard]] uint32_t passableCount() const noexcept;

    /**
     * @brief Invokes @p callback for each passable east/south adjacency pair.
     * @ingroup hbrick_grid
     *
     * Iterates cells in row-major order and reports undirected adjacencies
     * along east and south only, avoiding duplicate edge enumeration.
     *
     * @tparam Callback Callable with signature
     *         @c void(GridCoord from, GridCoord to, Direction direction).
     * @param callback Function invoked for each qualifying pair.
     */
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
    /** @brief Maps @p coord to a dense index into @ref passable_. @ingroup hbrick_grid */
    [[nodiscard]] std::size_t cellIndex(GridCoord coord) const noexcept;

    GridDimensions dimensions_;
    std::vector<uint8_t> passable_;
};

}  // namespace hbrick
