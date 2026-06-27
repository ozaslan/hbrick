/**
 * @file super_tile_oracle.hpp
 * @ingroup hbrick_tile
 * @brief Independent oracles for verifying composed super-tile boundary summaries.
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/core/grid_coord.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/super_tile_summary.hpp"
#include "hbrick/tile/tile_slot.hpp"

namespace hbrick {

class BrickIndex;
class DirectedGridGraph;
class MazeLayout;

/**
 * @brief Warshall closure on passable cells induced inside one region bbox (Oracle B ground truth).
 * @ingroup hbrick_tile
 */
struct RegionCellClosure {
    /** @brief Passable cell coordinates in row-major order. @ingroup hbrick_tile */
    std::vector<GridCoord> cell_coords;
    /** @brief Transitive closure on @ref cell_coords. @ingroup hbrick_tile */
    BitMatrix closure;
    /** @brief Outcome of building the closure. @ingroup hbrick_tile */
    BaselineStatus status = BaselineStatus::NotRun;
};

/**
 * @brief Builds @c Gamma_U from all global ports whose coordinates lie in @p parent_bbox.
 * @ingroup hbrick_tile
 */
[[nodiscard]] GammaOrdering buildRegionGammaFromPortIndex(
    const BrickIndex& brick_index,
    const TileSlot& parent_bbox
);

/**
 * @brief Builds a flat region port adjacency matrix on @p gamma (Oracle A construction).
 * @ingroup hbrick_tile
 */
[[nodiscard]] BitMatrix buildFlatRegionPortAdjacency(
    const BrickIndex& brick_index,
    const GammaOrdering& gamma
);

/**
 * @brief Runs Warshall on the flat region port graph and projects to the exterior (Oracle A ground truth).
 * @ingroup hbrick_tile
 */
[[nodiscard]] BitMatrix buildFlatRegionPortBoundarySummary(
    const BrickIndex& brick_index,
    const TileSlot& parent_bbox,
    const GammaOrdering& gamma,
    uint64_t max_memory_bytes
);

/**
 * @brief Builds an exact cell-level closure oracle inside @p region_bbox (Oracle B).
 * @ingroup hbrick_tile
 */
[[nodiscard]] RegionCellClosure buildRegionCellClosure(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSlot& region_bbox,
    uint64_t max_memory_bytes
);

/**
 * @brief Returns whether @p boundary_summary matches @p region_closure on @p exterior_ports.
 * @ingroup hbrick_tile
 */
[[nodiscard]] bool boundarySummaryMatchesCellClosure(
    const RegionCellClosure& region_closure,
    std::span<const GridCoord> exterior_ports,
    const BitMatrix& boundary_summary
) noexcept;

}  // namespace hbrick
