/**
 * @file grid_graph_renderer.hpp
 * @ingroup hbrick_viz
 * @brief SVG rendering of maze layouts and their directed graphs.
 */

#pragma once

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/viz/svg_canvas.hpp"

namespace hbrick {

/**
 * @brief Static renderer that draws grid cells and directed edges to SVG.
 * @ingroup hbrick_viz
 *
 * Blocked cells, passable cells, and edge arrows are mapped to simple geometric
 * primitives on an @ref hbrick::SvgCanvas for debugging and test artifacts.
 */
class GridGraphRenderer {
public:
    /**
     * @brief Renders @p grid and @p graph into a new SVG canvas.
     * @ingroup hbrick_viz
     *
     * @param grid Passable cell layout defining vertex positions.
     * @param graph Directed graph aligned with the same grid dimensions.
     * @param cell_size Pixel size of each grid cell in the output SVG.
     * @return Populated SVG canvas ready for @ref hbrick::SvgCanvas::toString.
     */
    [[nodiscard]] static SvgCanvas render(
        const MazeLayout& grid,
        const DirectedGridGraph& graph,
        double cell_size = 24.0
    );
};

}  // namespace hbrick
