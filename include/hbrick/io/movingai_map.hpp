/**
 * @file movingai_map.hpp
 * @ingroup hbrick_io
 * @brief Raw MovingAI benchmark map and its conversion to @ref hbrick::MazeLayout.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/grid/grid_dimensions.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace hbrick {

/**
 * @brief Semantic class of a MovingAI map cell character.
 * @ingroup hbrick_io
 *
 * Covers the standard octile vocabulary documented at
 * https://www.movingai.com/benchmarks/formats.html plus the letter-coded
 * terrain classes used by the weighted benchmark set.
 */
enum class MovingAiTerrain : uint8_t {
    /** @brief Regular passable ground (@c '.' or @c 'G'). @ingroup hbrick_io */
    Ground,
    /** @brief Trees, impassable (@c 'T'). @ingroup hbrick_io */
    Tree,
    /** @brief Swamp, passable from regular terrain (@c 'S'). @ingroup hbrick_io */
    Swamp,
    /** @brief Water, traversable but not passable from terrain (@c 'W'). @ingroup hbrick_io */
    Water,
    /** @brief Out of bounds (@c '@' or @c 'O'). @ingroup hbrick_io */
    OutOfBounds,
    /** @brief Letter-coded cost class from the weighted set (@c 'A'-@c 'Z' not otherwise mapped). @ingroup hbrick_io */
    WeightedTerrain,
    /** @brief Any character outside the documented vocabulary. @ingroup hbrick_io */
    Unknown,
};

/**
 * @brief Classifies a raw MovingAI cell character.
 * @ingroup hbrick_io
 *
 * @param cell Raw character from a @c .map grid.
 * @return The semantic terrain class of @p cell.
 */
[[nodiscard]] MovingAiTerrain movingAiTerrainFromChar(char cell) noexcept;

/**
 * @brief Returns a human-readable name for a terrain class.
 * @ingroup hbrick_io
 *
 * @param terrain Terrain class to describe.
 * @return Static string such as "ground" or "out of bounds".
 */
[[nodiscard]] const char* movingAiTerrainName(MovingAiTerrain terrain) noexcept;

/**
 * @brief Policy deciding which MovingAI terrain classes count as passable.
 * @ingroup hbrick_io
 *
 * MovingAI cells are richer than the binary passable/blocked model of
 * @ref hbrick::MazeLayout, so conversion requires an explicit choice. The
 * policy lives in the io adapter layer on purpose: @ref hbrick::MazeLayout
 * stays dataset-agnostic.
 */
enum class MovingAiPassabilityPolicy : uint8_t {
    /** @brief Only @ref MovingAiTerrain::Ground is passable. @ingroup hbrick_io */
    GroundOnly,
    /** @brief Ground and swamp are passable. @ingroup hbrick_io */
    GroundAndSwamp,
    /** @brief Ground, swamp, and water are passable. @ingroup hbrick_io */
    AllTraversable,
    /** @brief Ground, swamp, water, and weighted terrain letters are passable (weighted set). @ingroup hbrick_io */
    AnyTerrainLetter,
};

/**
 * @brief Returns whether a raw cell character is passable under @p policy.
 * @ingroup hbrick_io
 *
 * @param cell Raw character from a @c .map grid.
 * @param policy Passability policy to apply.
 * @return @c true when the cell's terrain class is passable under @p policy.
 */
[[nodiscard]] bool isMovingAiCellPassable(
    char cell,
    MovingAiPassabilityPolicy policy
) noexcept;

/**
 * @brief Immutable raw view of one MovingAI benchmark map.
 * @ingroup hbrick_io
 *
 * Stores the original cell characters in row-major order together with the
 * header metadata, preserving the full dataset vocabulary (swamp, water,
 * weighted terrain letters) that a @ref hbrick::MazeLayout cannot represent.
 * Use @ref toMazeLayout to enter the algorithmic pipeline.
 *
 * @see docs/representations.md for the layering rationale.
 */
class MovingAiMap {
public:
    /** @brief Constructs an empty zero-sized map. @ingroup hbrick_io */
    MovingAiMap() = default;

    /**
     * @brief Constructs a map from parsed header data and cells.
     * @ingroup hbrick_io
     *
     * @param dimensions Grid width and height from the header.
     * @param type_name Value of the header @c type field (usually "octile").
     * @param cells Row-major cell characters; size must equal @c dimensions.numCells().
     */
    MovingAiMap(GridDimensions dimensions, std::string type_name, std::vector<char> cells);

    /** @brief Returns the grid width in cells. @return The grid width in cells. @ingroup hbrick_io */
    [[nodiscard]] uint32_t width() const noexcept { return dimensions_.width; }
    /** @brief Returns the grid height in cells. @return The grid height in cells. @ingroup hbrick_io */
    [[nodiscard]] uint32_t height() const noexcept { return dimensions_.height; }
    /** @brief Returns a copy of the stored width/height metadata. @return A copy of the stored width/height metadata. @ingroup hbrick_io */
    [[nodiscard]] GridDimensions dimensions() const noexcept { return dimensions_; }
    /** @brief Returns the header @c type field value. @return The header type string. @ingroup hbrick_io */
    [[nodiscard]] const std::string& typeName() const noexcept { return type_name_; }
    /** @brief Returns the row-major raw cell characters. @return The raw cell vector. @ingroup hbrick_io */
    [[nodiscard]] const std::vector<char>& cells() const noexcept { return cells_; }

    /**
     * @brief Returns the raw character of the cell at @p coord.
     * @ingroup hbrick_io
     *
     * @param coord Cell coordinate.
     * @return The stored character, or @c '@' when @p coord is out of bounds.
     */
    [[nodiscard]] char cellAt(GridCoord coord) const noexcept;

    /**
     * @brief Returns the terrain class of the cell at @p coord.
     * @ingroup hbrick_io
     *
     * @param coord Cell coordinate.
     * @return Terrain class; @ref MovingAiTerrain::OutOfBounds when @p coord is outside the grid.
     */
    [[nodiscard]] MovingAiTerrain terrainAt(GridCoord coord) const noexcept;

    /**
     * @brief Converts this map to a @ref hbrick::MazeLayout under @p policy.
     * @ingroup hbrick_io
     *
     * The explicit conversion point between the dataset-specific raw
     * representation and the dataset-agnostic algorithmic entry layer.
     *
     * @param policy Passability policy applied per cell.
     * @return Layout of identical dimensions with passability derived from @p policy.
     */
    [[nodiscard]] MazeLayout toMazeLayout(MovingAiPassabilityPolicy policy) const;

private:
    GridDimensions dimensions_;
    std::string type_name_;
    std::vector<char> cells_;
};

}  // namespace hbrick
