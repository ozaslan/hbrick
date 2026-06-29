/**
 * @file connected_components.hpp
 * @ingroup hbrick_graph
 * @brief Undirected connected-component utilities on directed CSR graphs.
 */

#pragma once

#include <cstdint>
#include <span>

namespace hbrick {

class BitMatrix;
class CsrGraph;

/**
 * @brief Returns the number of vertices in the largest undirected component.
 * @ingroup hbrick_graph
 *
 * Treats each directed edge as an undirected link between its endpoints.
 */
[[nodiscard]] uint32_t largestUndirectedComponentSize(const CsrGraph& graph) noexcept;

/**
 * @brief Returns the largest undirected component size in a reflexive adjacency matrix.
 * @ingroup hbrick_graph
 *
 * Treats @c adjacency[i][j] as an undirected link when @c i != @c j.
 */
[[nodiscard]] uint32_t largestUndirectedComponentSizeFromReflexiveAdjacency(
    const BitMatrix& adjacency
) noexcept;

/**
 * @brief Labels each vertex with its undirected component id.
 * @ingroup hbrick_graph
 *
 * @param graph Input CSR graph.
 * @param component_labels Must hold at least @c graph.numVertices() entries; on output,
 *        @p component_labels[v] is a dense id in @c [0, num_components).
 * @return Size of the largest undirected component.
 */
[[nodiscard]] uint32_t fillUndirectedComponentLabels(
    const CsrGraph& graph,
    std::span<uint32_t> component_labels
) noexcept;

}  // namespace hbrick
