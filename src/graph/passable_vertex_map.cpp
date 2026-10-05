#include "hbrick/graph/passable_vertex_map.hpp"

#include "hbrick/graph/csr_graph_builder.hpp"

namespace hbrick {

PassableVertexMap PassableVertexMap::fromLayout(const MazeLayout& layout) {
    PassableVertexMap map;
    const uint32_t grid_count = layout.numVertices();
    map.grid_to_compact_.assign(grid_count, kInvalidVertexId);
    map.compact_to_grid_.reserve(layout.passableCount());

    for (uint32_t grid_vertex = 0U; grid_vertex < grid_count; ++grid_vertex) {
        if (!layout.isPassable(VertexId{grid_vertex})) {
            continue;
        }
        const uint32_t compact = static_cast<uint32_t>(map.compact_to_grid_.size());
        map.grid_to_compact_[grid_vertex] = compact;
        map.compact_to_grid_.push_back(grid_vertex);
    }
    return map;
}

uint32_t PassableVertexMap::toCompact(const uint32_t grid_vertex) const noexcept {
    if (grid_vertex >= grid_to_compact_.size()) {
        return kInvalidVertexId;
    }
    return grid_to_compact_[grid_vertex];
}

uint32_t PassableVertexMap::toGrid(const uint32_t compact_vertex) const noexcept {
    if (compact_vertex >= compact_to_grid_.size()) {
        return kInvalidVertexId;
    }
    return compact_to_grid_[compact_vertex];
}

CsrGraph inducePassableCsr(
    const DirectedGridGraph& grid_graph,
    const PassableVertexMap& map
) {
    CsrGraphBuilder builder{map.compactVertexCount()};
    const CsrGraph& grid_csr = grid_graph.csrGraph();
    const uint32_t grid_vertices = grid_csr.numVertices();

    for (uint32_t grid_from = 0U; grid_from < grid_vertices; ++grid_from) {
        const uint32_t compact_from = map.toCompact(grid_from);
        if (compact_from == kInvalidVertexId) {
            continue;
        }
        for (const uint32_t grid_to : grid_csr.outNeighbors(grid_from)) {
            const uint32_t compact_to = map.toCompact(grid_to);
            if (compact_to == kInvalidVertexId) {
                continue;
            }
            builder.addEdge(compact_from, compact_to);
        }
    }
    return std::move(builder).build();
}

std::vector<uint32_t> collectPassableGridVertices(const MazeLayout& layout) {
    const PassableVertexMap map = PassableVertexMap::fromLayout(layout);
    const std::span<const uint32_t> passable = map.passableGridVertices();
    return {passable.begin(), passable.end()};
}

}  // namespace hbrick
