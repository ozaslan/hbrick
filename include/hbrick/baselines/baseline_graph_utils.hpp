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
[[nodiscard]] bool sortedLabelsIntersect(
    std::span<const uint32_t> left,
    std::span<const uint32_t> right
) noexcept;

}  // namespace hbrick
