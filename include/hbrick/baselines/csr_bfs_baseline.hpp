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
 * Preprocessing copies the CSR graph. Each query invokes @ref hbrick::Bfs::reachable
 * with caller-provided scratch memory. Useful as a correctness oracle with
 * minimal preprocessing cost.
 */
class CsrBfsBaseline {
public:
    /**
     * @brief Stores a copy of @p graph for subsequent queries.
     * @ingroup hbrick_baselines
     *
     * @param graph Input directed graph.
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
    /** @brief Returns the stored CSR graph copy. @return The stored CSR graph copy. @ingroup hbrick_baselines */
    [[nodiscard]] const CsrGraph& graph() const noexcept { return graph_; }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    CsrGraph graph_;
};

}  // namespace hbrick
