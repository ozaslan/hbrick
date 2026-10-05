/**
 * @file csr_bfs_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief Per-query BFS reachability baseline retaining the CSR graph.
 */

#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

/**
 * @brief Reference baseline that stores the graph and runs BFS per query.
 * @ingroup hbrick_baselines
 *
 * Preprocessing binds the input graph by reference (no index is built).
 * Each query invokes @ref hbrick::Bfs::reachable with caller-provided scratch
 * memory. Useful as a correctness oracle with zero preprocessing cost.
 */
class CsrBfsBaseline {
public:
    /**
     * @brief Binds @p graph for subsequent queries without copying it.
     * @ingroup hbrick_baselines
     *
     * @p graph must outlive this baseline and remain valid for every @ref query call.
     */
    void preprocess(const CsrGraph& graph);

    /**
     * @brief Answers reachability by breadth-first search.
     * @ingroup hbrick_baselines
     *
     * @param source Source vertex index.
     * @param target Target vertex index.
     * @param scratch Reusable traversal workspace.
     * @return Reachability result from BFS.
     */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. @return The outcome of the most recent @ref preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }
    /** @brief Returns the bound CSR graph, or an empty graph when not preprocessed. @ingroup hbrick_baselines */
    [[nodiscard]] const CsrGraph& graph() const noexcept;

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    const CsrGraph* graph_ = nullptr;
};

}  // namespace hbrick
