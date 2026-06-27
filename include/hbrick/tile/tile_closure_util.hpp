/**
 * @file tile_closure_util.hpp
 * @ingroup hbrick_tile
 * @brief Local reflexive adjacency helpers for per-tile Kleene closure preprocessing.
 */

#pragma once

#include <cstdint>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/tile/hbrick_config.hpp"

namespace hbrick {

/** @brief Returns whether @p max_memory_bytes disables closure preflight checks. @ingroup hbrick_tile */
[[nodiscard]] bool isUnlimitedMemoryBudget(uint64_t max_memory_bytes) noexcept;

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

/**
 * @brief Returns the largest undirected component size in a reflexive adjacency matrix.
 * @ingroup hbrick_tile
 *
 * Treats @c adjacency[i][j] as an undirected link when @c i != @c j.
 */
[[nodiscard]] uint32_t largestUndirectedComponentSizeFromAdjacency(
    const BitMatrix& adjacency
) noexcept;

}  // namespace hbrick
