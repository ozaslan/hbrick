/**
 * @file tile_closure_util.hpp
 * @ingroup hbrick_tile
 * @brief Local reflexive adjacency helpers for per-tile Kleene closure preprocessing.
 */

#pragma once

#include <cstdint>
#include <span>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_slot.hpp"

namespace hbrick {

/** @brief Returns whether @p max_memory_bytes disables closure preflight checks. @ingroup hbrick_tile */
[[nodiscard]] bool isUnlimitedMemoryBudget(uint64_t max_memory_bytes) noexcept;

/** @brief Exact heap bytes for a dense @p num_rows × @p num_cols @ref BitMatrix. @ingroup hbrick_tile */
[[nodiscard]] uint64_t exactBitMatrixStorageBytes(
    uint32_t num_rows,
    uint32_t num_cols
) noexcept;

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

/**
 * @brief Builds the CSR subgraph induced by @p global_vertices inside @p bbox.
 * @ingroup hbrick_tile
 *
 * Uses an O(k log k) sorted local lookup table where @c k = @p global_vertices.size(),
 * not a full-graph vertex map.
 */
[[nodiscard]] CsrGraph buildInducedSubgraph(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSlot& bbox,
    std::span<const uint32_t> global_vertices
);

}  // namespace hbrick
