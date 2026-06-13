#include "hbrick/graph/bfs.hpp"

namespace hbrick {

ReachabilityAnswer Bfs::reachable(
    const CsrGraph& graph,
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) noexcept {
    const uint32_t num_vertices = graph.numVertices();
    if (source >= num_vertices || target >= num_vertices) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source == target) {
        return ReachabilityAnswer::Reachable;
    }

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

            if (neighbor == target) {
                return ReachabilityAnswer::Reachable;
            }

            visited[neighbor] = mark;
            queue.push_back(neighbor);
        }
    }

    return ReachabilityAnswer::Unreachable;
}

uint32_t Bfs::reachableCount(
    const CsrGraph& graph,
    const uint32_t source,
    GraphSearchScratch& scratch
) noexcept {
    const uint32_t num_vertices = graph.numVertices();
    if (source >= num_vertices) {
        return 0U;
    }

    const uint32_t mark = scratch.nextMark();
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<uint32_t>& queue = scratch.queue();
    queue.clear();

    visited[source] = mark;
    queue.push_back(source);
    uint32_t count = 1U;

    std::size_t head = 0;
    while (head < queue.size()) {
        const uint32_t vertex = queue[head];
        ++head;

        for (const uint32_t neighbor : graph.outNeighbors(vertex)) {
            if (visited[neighbor] == mark) {
                continue;
            }

            visited[neighbor] = mark;
            queue.push_back(neighbor);
            ++count;
        }
    }

    return count;
}

}  // namespace hbrick
