/**
 * @file tile_micro_bfs.cpp
 * @brief Implementation of tile-confined micro-BFS.
 */

#include "hbrick/graph/tile_micro_bfs.hpp"

#include <cassert>

namespace hbrick {

ReachabilityAnswer TileMicroBfs::reachable(
    const DirectedGridGraph& graph,
    const GridCoord slot_origin,
    const uint32_t slot_width,
    const uint32_t slot_height,
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) noexcept {
    const uint32_t num_vertices = graph.csrGraph().numVertices();
    assert(scratch.isSizedFor(num_vertices));
    if (source >= num_vertices || target >= num_vertices) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source == target) {
        return ReachabilityAnswer::Reachable;
    }

    const uint32_t max_x = slot_origin.x + slot_width;
    const uint32_t max_y = slot_origin.y + slot_height;

    const uint32_t mark = scratch.nextMark();
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<uint32_t>& queue = scratch.queue();
    queue.clear();

    visited[source] = mark;
    queue.push_back(source);

    std::size_t head = 0;
    while (head < queue.size()) {
        const uint32_t vertex = queue[head];
        ++head;

        for (const uint32_t neighbor : graph.outNeighbors(vertex)) {
            if (visited[neighbor] == mark) {
                continue;
            }

            const GridCoord coord = graph.coordFromVertex(neighbor);
            if (coord.x < slot_origin.x || coord.x >= max_x ||
                coord.y < slot_origin.y || coord.y >= max_y) {
                continue;
            }

            if (neighbor == target) {
                return ReachabilityAnswer::Reachable;
            }

            visited[neighbor] = mark;
            queue.push_back(neighbor);
        }
    }

    return ReachabilityAnswer::Unreachable;
}

}  // namespace hbrick
