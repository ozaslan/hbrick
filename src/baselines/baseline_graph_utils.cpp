#include "hbrick/baselines/baseline_graph_utils.hpp"

#include <algorithm>

#include "hbrick/graph/csr_graph_builder.hpp"

namespace hbrick {

CsrGraph buildTransposeGraph(const CsrGraph& graph) {
    CsrGraphBuilder builder{graph.numVertices()};
    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (const uint32_t target : graph.outNeighbors(source)) {
            builder.addEdge(target, source);
        }
    }
    return builder.build();
}

void collectForwardReachable(
    const CsrGraph& graph,
    const uint32_t source,
    GraphSearchScratch& scratch,
    std::vector<uint32_t>& reachable_vertices
) {
    reachable_vertices.clear();
    if (source >= graph.numVertices()) {
        return;
    }

    const uint32_t mark = scratch.nextMark();
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<uint32_t>& queue = scratch.queue();
    queue.clear();

    visited[source] = mark;
    queue.push_back(source);

    std::size_t head = 0U;
    while (head < queue.size()) {
        const uint32_t vertex = queue[head];
        ++head;

        for (const uint32_t neighbor : graph.outNeighbors(vertex)) {
            if (visited[neighbor] == mark) {
                continue;
            }

            visited[neighbor] = mark;
            queue.push_back(neighbor);
        }
    }

    reachable_vertices.reserve(queue.size());
    for (const uint32_t vertex : queue) {
        reachable_vertices.push_back(vertex);
    }
}

void sortUniqueLabelsInPlace(std::vector<uint32_t>& labels) {
    std::sort(labels.begin(), labels.end());
    const auto duplicate_begin = std::unique(labels.begin(), labels.end());
    labels.erase(duplicate_begin, labels.end());
}

bool sortedLabelsIntersect(
    const std::span<const uint32_t> left,
    const std::span<const uint32_t> right
) noexcept {
    std::size_t left_index = 0U;
    std::size_t right_index = 0U;

    while (left_index < left.size() && right_index < right.size()) {
        if (left[left_index] == right[right_index]) {
            return true;
        }

        if (left[left_index] < right[right_index]) {
            ++left_index;
        } else {
            ++right_index;
        }
    }

    return false;
}

}  // namespace hbrick
