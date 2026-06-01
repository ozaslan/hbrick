#pragma once

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/passable_grid.hpp"
#include "hbrick/viz/svg_canvas.hpp"

namespace hbrick {

class GridGraphRenderer {
public:
    [[nodiscard]] static SvgCanvas render(
        const PassableGrid& grid,
        const DirectedGridGraph& graph,
        double cell_size = 24.0
    );
};

}  // namespace hbrick
