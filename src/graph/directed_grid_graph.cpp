#include "hbrick/graph/directed_grid_graph.hpp"

#include <algorithm>

#include "hbrick/core/vertex_id.hpp"
#include "hbrick/grid/grid_dimensions.hpp"

namespace hbrick {

DirectedGridGraph::DirectedGridGraph(
    const uint32_t width,
    const uint32_t height,
    CsrGraph graph
)
    : width_(width), height_(height), graph_(std::move(graph)) {}

GridCoord DirectedGridGraph::coordFromVertex(const uint32_t vertex) const noexcept {
    return coordFromLinearIndex(width_, vertex);
}

uint32_t DirectedGridGraph::vertexFromCoord(const GridCoord coord) const noexcept {
    if (coord.x >= width_ || coord.y >= height_) {
        return kInvalidVertexId;
    }
    return coord.linearIndex(width_);
}

std::vector<Edge32> DirectedGridGraph::edges() const {
    return graph_.edges();
}

}  // namespace hbrick
