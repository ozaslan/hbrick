/**
 * @file closure_matrix_builder.hpp
 * @ingroup hbrick_baselines
 * @brief Helpers for constructing reflexive adjacency matrices from CSR graphs.
 */

#pragma once

#include <cstdint>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/graph/csr_graph.hpp"

namespace hbrick {

/**
 * @brief Utility functions for closure preprocessing pipelines.
 * @ingroup hbrick_baselines
 *
 * Estimates memory requirements and builds the reflexive adjacency matrix that
 * serves as input to @ref hbrick::BooleanClosure.
 */
class ClosureMatrixBuilder {
public:
    /**
     * @brief Estimates bytes required for a reflexive adjacency matrix.
     * @ingroup hbrick_baselines
     *
     * @param num_vertices Number of vertices @c V in the graph.
     * @return Approximate storage for a @c V x @c V bit matrix.
     */
    [[nodiscard]] static uint64_t estimateReflexiveAdjacencyBytes(uint32_t num_vertices) noexcept;

    /**
     * @brief Returns whether a reflexive adjacency matrix fits in the budget.
     * @ingroup hbrick_baselines
     *
     * @param num_vertices Number of vertices @c V.
     * @param max_memory_bytes Maximum allowed allocation in bytes.
     * @return True when the condition holds; otherwise false.
     */
    [[nodiscard]] static bool canAllocateReflexiveAdjacency(
        uint32_t num_vertices,
        uint64_t max_memory_bytes
    ) noexcept;

    /**
     * @brief Builds a reflexive adjacency bit matrix from @p graph.
     * @ingroup hbrick_baselines
     *
     * Sets the diagonal and every directed edge bit. Throws if the memory budget
     * would be exceeded.
     *
     * @param graph Source directed graph.
     * @param max_memory_bytes Maximum allowed allocation in bytes.
     * @return Square bit matrix with @c numRows == numCols == numVertices().
     */
    [[nodiscard]] static BitMatrix buildReflexiveAdjacencyOrThrow(
        const CsrGraph& graph,
        uint64_t max_memory_bytes
    );
};

}  // namespace hbrick
