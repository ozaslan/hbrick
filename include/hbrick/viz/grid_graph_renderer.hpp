/**
 * @file grid_graph_renderer.hpp
 * @ingroup hbrick_viz
 * @brief SVG rendering of maze layouts and their directed graphs.
 */

#pragma once

#include <string_view>

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/viz/svg_canvas.hpp"

namespace hbrick {

/**
 * @brief Colors and sizes for @ref GridGraphRenderer.
 * @ingroup hbrick_viz
 *
 * Defaults match the dataset-browser canvas: white free cells, dark blocked
 * cells, gray bidirectional seams, orange one-way arrows.
 */
struct GridGraphRenderOptions {
    /** @brief Pixel size of each grid cell. */
    double cell_size = 32.0;
    /** @brief When false, only occupancy cells are drawn. */
    bool draw_edges = true;
    /** @brief Optional caption drawn in the top-left cell (off for paper figures). */
    std::string_view label{};
    std::string_view passable_fill = "#ffffff";
    std::string_view blocked_fill = "#4d4d4d";
    std::string_view grid_stroke = "#666666";
    std::string_view bidirectional_stroke = "#7a7a84";
    std::string_view oneway_stroke = "#e67a14";
};

/**
 * @brief Static renderer that draws grid cells and directed edges to SVG.
 * @ingroup hbrick_viz
 *
 * Blocked cells, passable cells, and edge arrows are mapped to simple geometric
 * primitives on an @ref hbrick::SvgCanvas. Bidirectional adjacencies are a
 * single gray segment; one-way adjacencies get an orange arrowhead. The same
 * pairing is used by the dataset browser.
 */
class GridGraphRenderer {
public:
    /**
     * @brief Renders occupancy cells, and directed edges when @p options.draw_edges.
     * @ingroup hbrick_viz
     *
     * @param grid Passable cell layout defining vertex positions.
     * @param graph Directed graph aligned with the same grid dimensions.
     *        Ignored when @p options.draw_edges is false.
     * @param options Cell size, colors, and whether to draw orientation.
     */
    [[nodiscard]] static SvgCanvas render(
        const MazeLayout& grid,
        const DirectedGridGraph& graph,
        const GridGraphRenderOptions& options
    );

    /**
     * @brief Occupancy-only rendering (no orientation arrows).
     * @ingroup hbrick_viz
     */
    [[nodiscard]] static SvgCanvas renderOccupancy(
        const MazeLayout& grid,
        const GridGraphRenderOptions& options = {}
    );

    /**
     * @brief Renders @p grid and @p graph with the default cell size.
     * @ingroup hbrick_viz
     */
    [[nodiscard]] static SvgCanvas render(
        const MazeLayout& grid,
        const DirectedGridGraph& graph,
        double cell_size = 24.0
    );
};

}  // namespace hbrick
