#include "hbrick/tile/tile_closure_util.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <vector>

#include "hbrick/graph/connected_components.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/tile/hbrick_config.hpp"

namespace hbrick {

namespace {

}  // namespace

bool isUnlimitedMemoryBudget(const uint64_t max_memory_bytes) noexcept {
    return max_memory_bytes >= kHBrickUnlimitedMemoryBytes;
}

uint64_t exactBitMatrixStorageBytes(
    const uint32_t num_rows,
    const uint32_t num_cols
) noexcept {
    if (num_rows == 0U || num_cols == 0U) {
        return 0U;
    }
    const uint64_t words_per_row = BitVector::wordCount(static_cast<size_t>(num_cols));
    return static_cast<uint64_t>(num_rows) * sizeof(BitVector)
        + static_cast<uint64_t>(num_rows) * words_per_row * sizeof(uint64_t);
}

bool canAllocateTileReflexiveAdjacency(
    const uint32_t num_vertices,
    const uint64_t max_memory_bytes
) noexcept {
    if (isUnlimitedMemoryBudget(max_memory_bytes)) {
        return true;
    }
    return exactBitMatrixStorageBytes(num_vertices, num_vertices) <= max_memory_bytes;
}

BitMatrix buildTileReflexiveAdjacencyOrThrow(
    const CsrGraph& graph,
    const uint64_t max_memory_bytes
) {
    const uint32_t num_vertices = graph.numVertices();
    if (!canAllocateTileReflexiveAdjacency(num_vertices, max_memory_bytes)) {
        throw std::runtime_error(
            "Reflexive adjacency matrix exceeds configured memory limit"
        );
    }

    BitMatrix relation(num_vertices, num_vertices);

    for (uint32_t vertex = 0; vertex < num_vertices; ++vertex) {
        relation.set(vertex, vertex);
        for (const uint32_t neighbor : graph.outNeighbors(vertex)) {
            relation.set(vertex, neighbor);
        }
    }

    return relation;
}

uint32_t largestUndirectedComponentSizeFromAdjacency(
    const BitMatrix& adjacency
) noexcept {
    return largestUndirectedComponentSizeFromReflexiveAdjacency(adjacency);
}

CsrGraph buildInducedSubgraph(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSlot& bbox,
    const std::span<const uint32_t> global_vertices
) {
    const uint32_t local_count = static_cast<uint32_t>(global_vertices.size());
    CsrGraphBuilder builder{local_count};
    if (local_count == 0U || bbox.extent.width == 0U || bbox.extent.height == 0U) {
        return std::move(builder).build();
    }

    const uint32_t width = bbox.extent.width;
    const uint32_t x0 = bbox.origin.x;
    const uint32_t y0 = bbox.origin.y;

    std::vector<uint32_t> cell_to_local(
        static_cast<std::size_t>(width) * bbox.extent.height,
        std::numeric_limits<uint32_t>::max()
    );
    for (uint32_t local_index = 0U; local_index < local_count; ++local_index) {
        const GridCoord c = graph.coordFromVertex(global_vertices[local_index]);
        if (bbox.contains(c)) {
            const std::size_t cell =
                static_cast<std::size_t>(c.y - y0) * width + (c.x - x0);
            cell_to_local[cell] = local_index;
        }
    }

    for (uint32_t local_from = 0U; local_from < local_count; ++local_from) {
        const uint32_t global_from = global_vertices[local_from];
        for (const uint32_t global_to : graph.outNeighbors(global_from)) {
            const GridCoord to_coord = graph.coordFromVertex(global_to);
            if (!bbox.contains(to_coord) || !layout.isPassable(to_coord)) {
                continue;
            }

            const std::size_t cell =
                static_cast<std::size_t>(to_coord.y - y0) * width + (to_coord.x - x0);
            const uint32_t local_to = cell_to_local[cell];
            if (local_to != std::numeric_limits<uint32_t>::max()) {
                builder.addEdge(local_from, local_to);
            }
        }
    }

    return std::move(builder).build();
}

}  // namespace hbrick
