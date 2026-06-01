/**
 * @file full_closure_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief Preprocessed full transitive-closure reachability baseline.
 */

#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"

namespace hbrick {

/**
 * @brief Reference baseline that materializes the full reachability matrix.
 * @ingroup hbrick_baselines
 *
 * Preprocessing builds a reflexive transitive closure via @ref hbrick::BooleanClosure.
 * Queries are O(1) bit lookups but memory is O(V²). Preprocess may set
 * @ref status to @ref hbrick::BaselineStatus::OutOfMemory when @p max_memory_bytes
 * would be exceeded.
 */
class FullClosureBaseline {
public:
    /**
     * @brief Builds the transitive closure for @p graph if memory allows.
     * @ingroup hbrick_baselines
     *
     * @param graph Input directed graph.
     * @param max_memory_bytes Upper bound on closure storage; checked before allocation.
     */
    void preprocess(const CsrGraph& graph, uint64_t max_memory_bytes);

    /**
     * @brief Answers a reachability query using the precomputed closure.
     * @ingroup hbrick_baselines
     *
     * @param source Source vertex index.
     * @param target Target vertex index.
     * @return Reachability result read from the closure matrix.
     * @pre @ref status is @ref hbrick::BaselineStatus::Completed.
     */
    [[nodiscard]] ReachabilityAnswer query(uint32_t source, uint32_t target) const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. @return The outcome of the most recent @ref preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }
    /** @brief Returns the vertex count captured during preprocessing. @return The vertex count captured during preprocessing. @ingroup hbrick_baselines */
    [[nodiscard]] uint32_t numVertices() const noexcept { return num_vertices_; }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    uint32_t num_vertices_ = 0;
    BitMatrix closure_;
};

}  // namespace hbrick
