/**
 * @file baseline_graph_utils.hpp
 * @ingroup hbrick_baselines
 * @brief Shared preprocessing helpers for reachability index baselines.
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

/**
 * @brief Builds the transpose of @p graph with the same vertex count.
 * @ingroup hbrick_baselines
 */
[[nodiscard]] CsrGraph buildTransposeGraph(const CsrGraph& graph);

#include <cassert>

/**
 * @brief Collects all vertices forward-reachable from @p source, including @p source.
 * @ingroup hbrick_baselines
 */
void collectForwardReachable(
    const CsrGraph& graph,
    uint32_t source,
    GraphSearchScratch& scratch,
    std::vector<uint32_t>& reachable_vertices
);

/**
 * @brief Collects forward-reachable vertices, checking a cancellation predicate periodically.
 * @return True if completed, false if timed out.
 * @ingroup hbrick_baselines
 */
template <typename CancelFn>
bool collectForwardReachableTimed(
    const CsrGraph& graph,
    const uint32_t source,
    GraphSearchScratch& scratch,
    std::vector<uint32_t>& reachable_vertices,
    CancelFn&& is_timed_out
) {
    reachable_vertices.clear();
    if (source >= graph.numVertices()) {
        return true;
    }

    assert(scratch.isSizedFor(graph.numVertices()));
    const uint32_t mark = scratch.nextMark();
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<uint32_t>& queue = scratch.queue();
    queue.clear();

    visited[source] = mark;
    queue.push_back(source);

    std::size_t head = 0U;
    uint32_t step = 0U;
    while (head < queue.size()) {
        if ((++step & 1023U) == 0U && is_timed_out()) {
            return false;
        }
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
    return true;
}

/**
 * @brief Sorts @p labels in place and removes duplicate entries.
 * @ingroup hbrick_baselines
 */
void sortUniqueLabelsInPlace(std::vector<uint32_t>& labels);

/**
 * @brief Returns whether two sorted label lists share at least one label.
 * @ingroup hbrick_baselines
 *
 * Hot-path helper: performs no heap allocation.
 */
[[nodiscard]] inline bool sortedLabelsIntersect(
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
