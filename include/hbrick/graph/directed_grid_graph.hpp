#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/edge32.hpp"

namespace hbrick {

class DirectedGridGraphBuilder;

class DirectedGridGraph {
public:
    DirectedGridGraph() = default;

    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] uint32_t numVertices() const noexcept { return graph_.numVertices(); }
    [[nodiscard]] uint64_t numEdges() const noexcept { return graph_.numEdges(); }

    [[nodiscard]] std::span<const uint32_t> outNeighbors(uint32_t vertex) const noexcept {
        return graph_.outNeighbors(vertex);
    }

    [[nodiscard]] GridCoord coordFromVertex(uint32_t vertex) const noexcept;
    [[nodiscard]] uint32_t vertexFromCoord(GridCoord coord) const noexcept;

    [[nodiscard]] const CsrGraph& csrGraph() const noexcept { return graph_; }
    [[nodiscard]] std::vector<Edge32> edges() const;

private:
    friend class DirectedGridGraphBuilder;

    DirectedGridGraph(uint32_t width, uint32_t height, CsrGraph graph);

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    CsrGraph graph_;
};

}  // namespace hbrick
