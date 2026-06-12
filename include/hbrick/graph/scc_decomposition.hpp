/**
 * @file scc_decomposition.hpp
 * @ingroup hbrick_graph
 * @brief Strongly connected component decomposition of directed graphs.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

/**
 * @brief Strongly connected component (SCC) labeling of a directed graph.
 * @ingroup hbrick_graph
 *
 * Produced by @ref compute using Kosaraju's two-pass DFS: finish-order DFS on
 * the input graph, then component assignment via DFS on the transpose in
 * reverse finish order. Each vertex maps to a dense component id in
 * @c [0, numComponents()). Component ids are stable for the lifetime of the
 * decomposition object.
 */
class SccDecomposition {
public:
    /** @brief Constructs an empty decomposition. @ingroup hbrick_graph */
    SccDecomposition() = default;

    /**
     * @brief Computes SCCs for @p graph using Kosaraju's two-pass DFS.
     * @ingroup hbrick_graph
     *
     * Uses @p scratch for visited marks and stack storage across both passes.
     *
     * @param graph Input directed graph.
     * @param scratch Reusable workspace; resized internally as needed.
     * @return Decomposition assigning each vertex a component id.
     */
    [[nodiscard]] static SccDecomposition compute(
        const CsrGraph& graph,
        GraphSearchScratch& scratch
    );

    /** @brief Returns the vertex count of the decomposed graph. @return The vertex count of the decomposed graph. @ingroup hbrick_graph */
    [[nodiscard]] uint32_t numVertices() const noexcept { return num_vertices_; }
    /** @brief Returns the number of strongly connected components found. @return The number of strongly connected components found. @ingroup hbrick_graph */
    [[nodiscard]] uint32_t numComponents() const noexcept { return num_components_; }

    /**
     * @brief Returns the component id of @p vertex.
     * @ingroup hbrick_graph
     *
     * @param vertex Zero-based vertex index.
     * @return Dense component id in @c [0, numComponents()).
     */
    [[nodiscard]] uint32_t componentOf(uint32_t vertex) const noexcept;

private:
    SccDecomposition(uint32_t num_vertices, uint32_t num_components, std::vector<uint32_t> component_ids);

    uint32_t num_vertices_ = 0;
    uint32_t num_components_ = 0;
    std::vector<uint32_t> component_ids_;
};

}  // namespace hbrick
