/**
 * @file base_tile_summary.hpp
 * @ingroup hbrick_tile
 * @brief Per-tile local closure and boundary projections for flat BRICK.
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/tile_port_record.hpp"
#include "hbrick/tile/tile_slot.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/**
 * @brief Preprocessed reachability data for one base tile slot (layers A + B).
 * @ingroup hbrick_tile
 *
 * Holds Kleene-star closure on the induced intra-tile passable subgraph plus
 * boundary projections @c S_T, @c R_VB, and @c R_BV.
 */
struct BaseTileSummary {
    /** @brief Slot geometry this summary was built from. @ingroup hbrick_tile */
    TileSlot slot{};
    /** @brief Outcome of building this summary. @ingroup hbrick_tile */
    BaselineStatus status = BaselineStatus::NotRun;

    /** @brief Local vertex coordinates in row-major passable order. @ingroup hbrick_tile */
    std::vector<GridCoord> local_coords;
    /** @brief Global graph vertex id per local index. @ingroup hbrick_tile */
    std::vector<uint32_t> global_vertices;
    /** @brief Passable boundary ports in canonical N→E→S→W order. @ingroup hbrick_tile */
    std::vector<TilePort> ports;

    /** @brief Transitive closure on intra-tile passable cells. @ingroup hbrick_tile */
    BitMatrix local_closure;
    /** @brief Boundary-to-boundary reachability @c S_T. @ingroup hbrick_tile */
    BitMatrix boundary_summary;
    /** @brief Cell-to-boundary attachment @c R_VB. @ingroup hbrick_tile */
    BitMatrix vertex_to_boundary;
    /** @brief Boundary-to-cell attachment @c R_BV. @ingroup hbrick_tile */
    BitMatrix boundary_to_vertex;

    /** @brief Number of local passable cells in this tile. @ingroup hbrick_tile */
    [[nodiscard]] uint32_t numLocalVertices() const noexcept {
        return static_cast<uint32_t>(local_coords.size());
    }

    /** @brief Number of exported boundary ports. @ingroup hbrick_tile */
    [[nodiscard]] uint32_t numPorts() const noexcept {
        return static_cast<uint32_t>(ports.size());
    }
};

/**
 * @brief Builds one base tile summary from a global grid graph.
 * @ingroup hbrick_tile
 *
 * Collects passable cells inside @p slot, runs Kleene squaring closure on the induced subgraph,
 * and projects @c S_T, @c R_VB, and @c R_BV. Sets @ref BaseTileSummary::status to
 * @ref BaselineStatus::SkippedByPolicy when the closure matrix exceeds @p max_memory_bytes.
 *
 * @param graph Global directed grid graph.
 * @param layout Passability bitmap aligned with @p graph.
 * @param slot Tile slot to summarize.
 * @param max_memory_bytes Maximum bytes allowed for the local closure matrix.
 * @param closure_nanoseconds When non-null, receives adjacency-build + Kleene closure time only.
 * @param closure_scratch Optional reusable M×M scratch for Kleene squaring across tiles.
 */
[[nodiscard]] BaseTileSummary buildBaseTile(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSlot& slot,
    uint64_t max_memory_bytes,
    uint64_t* closure_nanoseconds = nullptr,
    BitMatrix* closure_scratch = nullptr
);

}  // namespace hbrick
