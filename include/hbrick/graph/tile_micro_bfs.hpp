/**
 * @file tile_micro_bfs.hpp
 * @ingroup hbrick_graph
 * @brief Cache-hot micro-BFS reachability confined strictly to a single tile.
 */

#pragma once

#include <cstdint>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace hbrick {

/**
 * @brief Static helper running BFS confined strictly within a bounding box.
 * @ingroup hbrick_graph
 *
 * Traverses only directed edges whose target vertex falls within the specified
 * tile bounds [origin.x, origin.x + width) x [origin.y, origin.y + height).
 * Uses pre-allocated scratch to guarantee zero heap allocations.
 */
class TileMicroBfs {
public:
    /**
     * @brief Tests directed reachability from @p source to @p target within the tile.
     * @ingroup hbrick_graph
     *
     * @param graph Global directed grid graph.
     * @param slot_origin Minimum coordinate of the tile (inclusive).
     * @param slot_width Width of the tile in grid cells.
     * @param slot_height Height of the tile in grid cells.
     * @param source Source vertex ID (must lie inside the tile).
     * @param target Target vertex ID (must lie inside the tile).
     * @param scratch Pre-allocated search scratch sized for @p graph.
     * @return Reachable if an intra-tile path exists; otherwise Unreachable.
     */
    [[nodiscard]] static ReachabilityAnswer reachable(
        const DirectedGridGraph& graph,
        GridCoord slot_origin,
        uint32_t slot_width,
        uint32_t slot_height,
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) noexcept;

    /**
     * @brief Overload accepting MazeLayout for compatibility.
     * @ingroup hbrick_graph
     */
    [[nodiscard]] static ReachabilityAnswer reachable(
        const DirectedGridGraph& graph,
        const MazeLayout& /*layout*/,
        GridCoord slot_origin,
        uint32_t slot_width,
        uint32_t slot_height,
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) noexcept {
        return reachable(graph, slot_origin, slot_width, slot_height, source, target, scratch);
    }
};

}  // namespace hbrick
