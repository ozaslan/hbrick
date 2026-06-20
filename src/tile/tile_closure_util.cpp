#include "hbrick/tile/tile_closure_util.hpp"

#include <stdexcept>

namespace hbrick {

namespace {

constexpr uint64_t kBitsPerWord = 64U;

[[nodiscard]] uint64_t wordsPerRow(const uint32_t num_vertices) noexcept {
    return (static_cast<uint64_t>(num_vertices) + (kBitsPerWord - 1U)) / kBitsPerWord;
}

}  // namespace

uint64_t estimateTileReflexiveAdjacencyBytes(const uint32_t num_vertices) noexcept {
    const uint64_t rows = num_vertices;
    return rows * wordsPerRow(num_vertices) * sizeof(uint64_t);
}

bool canAllocateTileReflexiveAdjacency(
    const uint32_t num_vertices,
    const uint64_t max_memory_bytes
) noexcept {
    return estimateTileReflexiveAdjacencyBytes(num_vertices) <= max_memory_bytes;
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

}  // namespace hbrick
