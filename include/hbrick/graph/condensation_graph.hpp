/**
 * @file condensation_graph.hpp
 * @ingroup hbrick_graph
 * @brief SCC condensation of a directed graph into a DAG.
 */

#pragma once

#include <cstdint>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/scc_decomposition.hpp"

namespace hbrick {

/**
 * @brief Bundles an SCC decomposition with the induced component DAG.
 * @ingroup hbrick_graph
 *
 * The condensation replaces each strongly connected component with a single
 * super-node. Edges between components exist when any cross-component arc
 * appears in the original graph. The result is always a directed acyclic graph
 * suitable for @ref hbrick::DagReachability.
 */
class CondensationGraph {
public:
    /** @brief Constructs an empty condensation. @ingroup hbrick_graph */
    CondensationGraph() = default;

    /**
     * @brief Builds a condensation from @p graph and a precomputed decomposition.
     * @ingroup hbrick_graph
     *
     * @param graph Original directed graph.
     * @param decomposition SCC labeling for @p graph.
     * @return Condensation storing both the labeling and component DAG.
     */
    [[nodiscard]] static CondensationGraph fromGraph(
        const CsrGraph& graph,
        const SccDecomposition& decomposition
    );

    /** @brief Returns the number of SCC super-nodes. @return The number of SCC super-nodes. @ingroup hbrick_graph */
    [[nodiscard]] uint32_t numComponents() const noexcept { return decomposition_.numComponents(); }

    /**
     * @brief Returns the component id of an original-graph vertex.
     * @ingroup hbrick_graph
     *
     * @param vertex Vertex index in the original graph.
     * @return Documented value.
     */
    [[nodiscard]] uint32_t componentOf(uint32_t vertex) const noexcept {
        return decomposition_.componentOf(vertex);
    }

    /** @brief Returns the CSR graph whose vertices are component ids. @return The CSR graph whose vertices are component ids. @ingroup hbrick_graph */
    [[nodiscard]] const CsrGraph& dag() const noexcept { return dag_; }
    /** @brief Returns the underlying SCC decomposition. @return The underlying SCC decomposition. @ingroup hbrick_graph */
    [[nodiscard]] const SccDecomposition& decomposition() const noexcept { return decomposition_; }

private:
    CondensationGraph(SccDecomposition decomposition, CsrGraph dag);

    SccDecomposition decomposition_;
    CsrGraph dag_;
};

}  // namespace hbrick
