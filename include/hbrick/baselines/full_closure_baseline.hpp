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
     * @brief Starts incremental transitive-closure preprocessing.
     * @ingroup hbrick_baselines
     *
     * After a successful start, call @ref stepPreprocessPivots until it returns
     * @c true or invoke @ref abortPreprocessSkippedByPolicy.
     */
    void beginPreprocess(const CsrGraph& graph, uint64_t max_memory_bytes);

    /**
     * @brief Runs up to @p pivot_count Warshall pivot steps.
     * @ingroup hbrick_baselines
     *
     * @return @c true when preprocessing finished or was not active.
     */
    [[nodiscard]] bool stepPreprocessPivots(uint32_t pivot_count) noexcept;

    /** @brief Aborts incremental preprocessing and marks @ref SkippedByPolicy. @ingroup hbrick_baselines */
    void abortPreprocessSkippedByPolicy() noexcept;

    /** @brief Returns completed Warshall pivot count during incremental preprocessing. @ingroup hbrick_baselines */
    [[nodiscard]] uint32_t preprocessPivotsCompleted() const noexcept {
        return next_pivot_;
    }

    /** @brief Returns total Warshall pivots expected for the active graph. @ingroup hbrick_baselines */
    [[nodiscard]] uint32_t preprocessPivotTotal() const noexcept { return num_vertices_; }

    /** @brief Returns @c true while incremental preprocessing is in progress. @ingroup hbrick_baselines */
    [[nodiscard]] bool preprocessActive() const noexcept { return preprocess_active_; }

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

    /**
     * @brief Returns closure-matrix storage bytes after successful preprocessing.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept;

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    uint32_t num_vertices_ = 0;
    uint32_t next_pivot_ = 0;
    bool preprocess_active_ = false;
    BitMatrix relation_;
    BitMatrix closure_;
};

}  // namespace hbrick
