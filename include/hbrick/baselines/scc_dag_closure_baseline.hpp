/**
 * @file scc_dag_closure_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief SCC condensation plus component-level transitive closure baseline.
 */

#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/scc_decomposition.hpp"

namespace hbrick {

/**
 * @brief Reference baseline combining SCC decomposition with DAG closure.
 * @ingroup hbrick_baselines
 *
 * Preprocessing computes strongly connected components, builds the condensation
 * DAG, and materializes its transitive closure at component granularity.
 * Queries map original vertices to components and perform O(1) closure lookups.
 * Memory is O(C²) in the number of components @c C rather than vertices @c V.
 */
class SccDagClosureBaseline {
public:
    /**
     * @brief Decomposes @p graph and builds the component closure if memory allows.
     * @ingroup hbrick_baselines
     *
     * @param graph Input directed graph.
     * @param scratch Workspace for SCC computation.
     * @param max_memory_bytes Upper bound on closure storage.
     */
    void preprocess(
        const CsrGraph& graph,
        GraphSearchScratch& scratch,
        uint64_t max_memory_bytes
    );

    /**
     * @brief Starts incremental component-closure preprocessing.
     * @ingroup hbrick_baselines
     *
     * After a successful start, call @ref stepPreprocessPivots until it returns
     * @c true or invoke @ref abortPreprocessSkippedByPolicy.
     */
    void beginPreprocess(
        const CsrGraph& graph,
        GraphSearchScratch& scratch,
        uint64_t max_memory_bytes
    );

    /**
     * @brief Runs up to @p pivot_count Warshall pivot steps on the condensation DAG.
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

    /** @brief Returns total Warshall pivots expected for the active condensation DAG. @ingroup hbrick_baselines */
    [[nodiscard]] uint32_t preprocessPivotTotal() const noexcept { return num_components_; }

    /** @brief Returns @c true while incremental preprocessing is in progress. @ingroup hbrick_baselines */
    [[nodiscard]] bool preprocessActive() const noexcept { return preprocess_active_; }

    /**
     * @brief Answers reachability via component ids and the precomputed closure.
     * @ingroup hbrick_baselines
     *
     * Vertices in the same SCC are mutually reachable.
     *
     * @param source Source vertex index in the original graph.
     * @param target Target vertex index in the original graph.
     * @return Reachability result derived from component closure.
     */
    [[nodiscard]] ReachabilityAnswer query(uint32_t source, uint32_t target) const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. @return The outcome of the most recent @ref preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

    /**
     * @brief Returns component-closure storage bytes after successful preprocessing.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept;

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    SccDecomposition decomposition_;
    uint32_t num_components_ = 0U;
    uint32_t next_pivot_ = 0U;
    bool preprocess_active_ = false;
    BitMatrix relation_;
    BitMatrix closure_;
};

}  // namespace hbrick
