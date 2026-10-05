#include "hbrick/viz/grid_graph_renderer.hpp"

#include <cmath>

namespace hbrick {

namespace {

double cellCenter(const double cell_size, const uint32_t index) {
    return (static_cast<double>(index) + 0.5) * cell_size;
}

[[nodiscard]] bool hasArc(
    const DirectedGridGraph& graph,
    const uint32_t from,
    const uint32_t to
) {
    for (const uint32_t next : graph.outNeighbors(from)) {
        if (next == to) {
            return true;
        }
    }
    return false;
}

void drawOccupancy(
    SvgCanvas& canvas,
    const MazeLayout& grid,
    const GridGraphRenderOptions& options
) {
    const double cell_size = options.cell_size;
    for (uint32_t y = 0; y < grid.height(); ++y) {
        for (uint32_t x = 0; x < grid.width(); ++x) {
            const GridCoord coord{x, y};
            const double rect_x = static_cast<double>(x) * cell_size;
            const double rect_y = static_cast<double>(y) * cell_size;
            const std::string_view fill =
                grid.isPassable(coord) ? options.passable_fill : options.blocked_fill;
            canvas.rect(rect_x, rect_y, cell_size, cell_size, fill, options.grid_stroke);
        }
    }
}

void drawAdjacency(
    SvgCanvas& canvas,
    const DirectedGridGraph& graph,
    const GridGraphRenderOptions& options,
    const uint32_t ux,
    const uint32_t uy,
    const uint32_t vx,
    const uint32_t vy
) {
    const uint32_t u = graph.vertexFromCoord(GridCoord{ux, uy});
    const uint32_t v = graph.vertexFromCoord(GridCoord{vx, vy});
    const bool forward = hasArc(graph, u, v);
    const bool backward = hasArc(graph, v, u);
    if (!forward && !backward) {
        return;
    }

    const double cell_size = options.cell_size;
    const double ax = cellCenter(cell_size, ux);
    const double ay = cellCenter(cell_size, uy);
    const double bx = cellCenter(cell_size, vx);
    const double by = cellCenter(cell_size, vy);
    const double shrink = 0.20;
    const double sax = ax + (bx - ax) * shrink;
    const double say = ay + (by - ay) * shrink;
    const double sbx = bx - (bx - ax) * shrink;
    const double sby = by - (by - ay) * shrink;
    const double stroke_width = 0.0625 * cell_size;

    if (forward && backward) {
        canvas.line(sax, say, sbx, sby, options.bidirectional_stroke, stroke_width);
        return;
    }

    const double tail_x = forward ? sax : sbx;
    const double tail_y = forward ? say : sby;
    const double tip_x = forward ? sbx : sax;
    const double tip_y = forward ? sby : say;
    canvas.line(tail_x, tail_y, tip_x, tip_y, options.oneway_stroke, stroke_width);

    const double dx = tip_x - tail_x;
    const double dy = tip_y - tail_y;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0e-6) {
        return;
    }
    const double head = 0.30 * cell_size;
    const double nx = dx / length;
    const double ny = dy / length;
    canvas.triangle(
        tip_x,
        tip_y,
        tip_x - nx * head - ny * head * 0.55,
        tip_y - ny * head + nx * head * 0.55,
        tip_x - nx * head + ny * head * 0.55,
        tip_y - ny * head - nx * head * 0.55,
        options.oneway_stroke
    );
}

void drawEdges(
    SvgCanvas& canvas,
    const MazeLayout& grid,
    const DirectedGridGraph& graph,
    const GridGraphRenderOptions& options
) {
    const uint32_t width = grid.width();
    const uint32_t height = grid.height();
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            if (!grid.isPassable(x, y)) {
                continue;
            }
            if (x + 1U < width && grid.isPassable(x + 1U, y)) {
                drawAdjacency(canvas, graph, options, x, y, x + 1U, y);
            }
            if (y + 1U < height && grid.isPassable(x, y + 1U)) {
                drawAdjacency(canvas, graph, options, x, y, x, y + 1U);
            }
        }
    }
}

}  // namespace

SvgCanvas GridGraphRenderer::render(
    const MazeLayout& grid,
    const DirectedGridGraph& graph,
    const GridGraphRenderOptions& options
) {
    const uint32_t width_pixels =
        static_cast<uint32_t>(std::ceil(static_cast<double>(grid.width()) * options.cell_size));
    const uint32_t height_pixels =
        static_cast<uint32_t>(std::ceil(static_cast<double>(grid.height()) * options.cell_size));
    SvgCanvas canvas{width_pixels, height_pixels};

    drawOccupancy(canvas, grid, options);
    if (options.draw_edges) {
        drawEdges(canvas, grid, graph, options);
    }
    if (!options.label.empty()) {
        canvas.text(4.0, options.cell_size * 0.75, options.label);
    }
    return canvas;
}

SvgCanvas GridGraphRenderer::renderOccupancy(
    const MazeLayout& grid,
    const GridGraphRenderOptions& options
) {
    GridGraphRenderOptions occupancy = options;
    occupancy.draw_edges = false;
    DirectedGridGraph unused;
    return render(grid, unused, occupancy);
}

SvgCanvas GridGraphRenderer::render(
    const MazeLayout& grid,
    const DirectedGridGraph& graph,
    const double cell_size
) {
    GridGraphRenderOptions options;
    options.cell_size = cell_size;
    return render(grid, graph, options);
}

}  // namespace hbrick
