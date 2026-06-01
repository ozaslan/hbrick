#include "hbrick/graph/dfs.hpp"

namespace hbrick {

ReachabilityAnswer Dfs::reachable(
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
    std::vector<uint32_t>& stack = scratch.stack();
    stack.clear();

    visited[source] = mark;
    stack.push_back(source);

    while (!stack.empty()) {
        const uint32_t vertex = stack.back();
        stack.pop_back();

        for (const uint32_t neighbor : graph.outNeighbors(vertex)) {
            if (visited[neighbor] == mark) {
                continue;
            }

            if (neighbor == target) {
                return ReachabilityAnswer::Reachable;
            }

            visited[neighbor] = mark;
            stack.push_back(neighbor);
        }
    }

    return ReachabilityAnswer::Unreachable;
}

}  // namespace hbrick
