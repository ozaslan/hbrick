/**
 * @file super_tile_summary.hpp
 * @ingroup hbrick_tile
 * @brief Composed boundary data for one H-BRICK super-tile region.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/core/grid_coord.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/tile_slot.hpp"

namespace hbrick {

/**
 * @brief Ordered port list used when composing one parent region.
 * @ingroup hbrick_tile
 */
struct GammaOrdering {
    /** @brief All child ports inside the parent bbox in deterministic order. @ingroup hbrick_tile */
    std::vector<GridCoord> ports;
};

/**
 * @brief Composed reachability summary for one grouped parent region.
 * @ingroup hbrick_tile
 */
struct SuperTileSummary {
    /** @brief Parent region geometry in the fine grid. @ingroup hbrick_tile */
    TileSlot slot{};
    /** @brief Outcome of composition. @ingroup hbrick_tile */
    BaselineStatus status = BaselineStatus::NotRun;

    /** @brief Ordered child ports @c Gamma_U used during composition. @ingroup hbrick_tile */
    GammaOrdering gamma{};
    /** @brief Exterior ports of @ref slot in canonical perimeter order. @ingroup hbrick_tile */
    std::vector<GridCoord> exterior_ports;
    /** @brief Maps each exterior port to its index in @ref gamma.ports. @ingroup hbrick_tile */
    std::vector<uint32_t> exterior_gamma_indices;

    /**
     * @brief Per active child: child-port → gamma index (@c UINT32_MAX if absent).
     * @ingroup hbrick_tile
     *
     * Query uses this for O(#set child ports) embedding instead of scanning embedding columns.
     */
    std::vector<std::vector<uint32_t>> child_port_to_gamma;
    /**
     * @brief Maps hierarchy child slot → active child index in @ref child_port_to_gamma.
     * @ingroup hbrick_tile
     *
     * Length matches the parent region's child count. Inactive children (no ports)
     * store @c UINT32_MAX.
     */
    std::vector<uint32_t> child_embedding_of;
    /** @brief Interface closure @c S̄_U on @ref gamma. @ingroup hbrick_tile */
    BitMatrix interface_closure;
    /**
     * @brief Transpose of @ref interface_closure for reverse attachment lifts at query time.
     * @ingroup hbrick_tile
     */
    BitMatrix interface_closure_transpose;
    /** @brief Exterior boundary summary @c S_U on @ref exterior_ports. @ingroup hbrick_tile */
    BitMatrix boundary_summary;
};

}  // namespace hbrick
