/**
 * @file super_tile_composer.hpp
 * @ingroup hbrick_tile
 * @brief Composes child boundary summaries into one parent super-tile region.
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/tile/port_index.hpp"
#include "hbrick/tile/seam_edge.hpp"
#include "hbrick/tile/super_tile_summary.hpp"
#include "hbrick/tile/tile_slot.hpp"

namespace hbrick {

class BaseTileSummary;

/**
 * @brief One child's boundary summary supplied to @ref composeSuperTile.
 * @ingroup hbrick_tile
 */
struct ChildBoundarySummary {
    /** @brief Child region geometry. @ingroup hbrick_tile */
    TileSlot slot{};
    /** @brief Child boundary ports in child-local order. @ingroup hbrick_tile */
    std::span<const GridCoord> port_coords;
    /** @brief Child boundary summary @c S_{C_i}. @ingroup hbrick_tile */
    const BitMatrix* boundary_summary = nullptr;
    /** @brief Base-tile index used for seam-edge child discrimination. @ingroup hbrick_tile */
    uint32_t tile_index = UINT32_MAX;
};

/**
 * @brief Builds @c Gamma_U: all child ports inside @p parent_bbox, deterministically ordered.
 * @ingroup hbrick_tile
 */
[[nodiscard]] GammaOrdering buildGammaOrdering(
    const TileSlot& parent_bbox,
    std::span<const ChildBoundarySummary> children
);

/**
 * @brief Builds embedding @c E_i mapping child ports into @p gamma indices.
 * @ingroup hbrick_tile
 */
[[nodiscard]] BitMatrix buildEmbedding(
    std::span<const GridCoord> child_port_coords,
    const GammaOrdering& gamma
);

/**
 * @brief Builds cross-child interface adjacency @c A_U^{iface} on @p gamma.
 * @ingroup hbrick_tile
 */
[[nodiscard]] BitMatrix buildIfaceAdjacency(
    const GammaOrdering& gamma,
    const PortIndex& port_index,
    std::span<const SeamEdge> seam_edges
);

/**
 * @brief Forms @c Â_U = (⊕_i E_i S_{C_i} E_i^T) ⊕ @p iface_adjacency on @p gamma.
 * @ingroup hbrick_tile
 */
[[nodiscard]] BitMatrix composeInterfaceAdjacency(
    const GammaOrdering& gamma,
    std::span<const ChildBoundarySummary> children,
    std::span<const BitMatrix> child_embeddings,
    const BitMatrix& iface_adjacency
);

/**
 * @brief Runs truncated Kleene squaring on @p composed_adjacency to produce @c S̄_U.
 * @ingroup hbrick_tile
 *
 * Uses @c ceil(log2(n_max)) squaring rounds where @c n_max is the largest undirected
 * component in @p composed_adjacency.
 *
 * @param composed_adjacency Reflexive interface relation @c I | A on @c Gamma_U.
 * @param scratch Optional reusable buffer; resized when dimensions change.
 */
[[nodiscard]] BitMatrix computeInterfaceClosure(
    BitMatrix composed_adjacency,
    BitMatrix* scratch = nullptr
);

/**
 * @brief Projects @p interface_closure to the exterior perimeter of @p parent_bbox.
 * @ingroup hbrick_tile
 */
[[nodiscard]] BitMatrix projectExternalSummary(
    const TileSlot& parent_bbox,
    const GammaOrdering& gamma,
    const BitMatrix& interface_closure
);

/**
 * @brief Composes one parent super-tile from completed child boundary summaries.
 * @ingroup hbrick_tile
 */
[[nodiscard]] SuperTileSummary composeSuperTile(
    const TileSlot& parent_bbox,
    std::span<const ChildBoundarySummary> children,
    const PortIndex& port_index,
    std::span<const SeamEdge> seam_edges,
    uint64_t max_memory_bytes,
    BitMatrix* closure_scratch = nullptr
);

/**
 * @brief Builds a @ref ChildBoundarySummary view from one @ref BaseTileSummary.
 * @ingroup hbrick_tile
 */
[[nodiscard]] ChildBoundarySummary childBoundaryFromBaseTile(
    const BaseTileSummary& summary,
    uint32_t tile_index,
    std::vector<GridCoord>& port_coord_storage
);

/**
 * @brief Builds a @ref ChildBoundarySummary view from one @ref SuperTileSummary child.
 * @ingroup hbrick_tile
 */
[[nodiscard]] ChildBoundarySummary childBoundaryFromSuperTile(
    const SuperTileSummary& summary,
    std::vector<GridCoord>& port_coord_storage
);

}  // namespace hbrick
