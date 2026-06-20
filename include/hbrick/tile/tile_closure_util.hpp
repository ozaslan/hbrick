/**
 * @file tile_closure_util.hpp
 * @ingroup hbrick_tile
 * @brief Local reflexive adjacency helpers for per-tile Warshall preprocessing.
 */

#pragma once

#include <cstdint>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/graph/csr_graph.hpp"

namespace hbrick {

/** @brief Estimates bytes for a reflexive @c V×V bit adjacency matrix. @ingroup hbrick_tile */
[[nodiscard]] uint64_t estimateTileReflexiveAdjacencyBytes(uint32_t num_vertices) noexcept;

/** @brief Returns whether a reflexive adjacency matrix fits in @p max_memory_bytes. @ingroup hbrick_tile */
[[nodiscard]] bool canAllocateTileReflexiveAdjacency(
    uint32_t num_vertices,
    uint64_t max_memory_bytes
) noexcept;

/**
 * @brief Builds reflexive adjacency from @p graph or throws when over budget.
 * @ingroup hbrick_tile
 */
[[nodiscard]] BitMatrix buildTileReflexiveAdjacencyOrThrow(
    const CsrGraph& graph,
    uint64_t max_memory_bytes
);

}  // namespace hbrick
