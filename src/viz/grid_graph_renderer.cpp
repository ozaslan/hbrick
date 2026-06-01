#include "hbrick/viz/grid_graph_renderer.hpp"

#include <cmath>

namespace hbrick {

namespace {

double cellCenter(const double cell_size, const uint32_t index) {
    return (static_cast<double>(index) + 0.5) * cell_size;
}

}  // namespace

SvgCanvas GridGraphRenderer::render(
    const PassableGrid& grid,
    const DirectedGridGraph& graph,
    const double cell_size
) {
    const uint32_t width_pixels = static_cast<uint32_t>(std::ceil(grid.width() * cell_size));
    const uint32_t height_pixels = static_cast<uint32_t>(std::ceil(grid.height() * cell_size));
    SvgCanvas canvas{width_pixels, height_pixels};

    for (uint32_t y = 0; y < grid.height(); ++y) {
        for (uint32_t x = 0; x < grid.width(); ++x) {
            const GridCoord coord{x, y};
            const double rect_x = static_cast<double>(x) * cell_size;
            const double rect_y = static_cast<double>(y) * cell_size;
            const std::string_view fill = grid.isPassable(coord) ? "#ffffff" : "#cccccc";
            canvas.rect(rect_x, rect_y, cell_size, cell_size, fill, "#666666");
        }
    }

    for (const Edge32& edge : graph.edges()) {
        const GridCoord from = graph.coordFromVertex(edge.from);
        const GridCoord to = graph.coordFromVertex(edge.to);

        canvas.line(
            cellCenter(cell_size, from.x),
            cellCenter(cell_size, from.y),
            cellCenter(cell_size, to.x),
            cellCenter(cell_size, to.y),
            "#3366cc"
        );
    }

    canvas.text(4.0, cell_size * 0.75, "H-BRICK grid graph");
    return canvas;
}

}  // namespace hbrick
