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

    /** @brief Embedding @c E_i per child (gamma rows by child-port columns). @ingroup hbrick_tile */
    std::vector<BitMatrix> child_embeddings;
    /** @brief Interface closure @c S̄_U on @ref gamma. @ingroup hbrick_tile */
    BitMatrix interface_closure;
    /** @brief Exterior boundary summary @c S_U on @ref exterior_ports. @ingroup hbrick_tile */
    BitMatrix boundary_summary;
};

}  // namespace hbrick
