/**
 * @file scc_dag_search_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief SCC condensation plus per-query DAG search baseline.
 */

#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/scc_decomposition.hpp"

namespace hbrick {

/**
 * @brief Reference baseline that searches the condensation DAG per query.
 * @ingroup hbrick_baselines
 *
 * Preprocessing computes SCCs and stores the component DAG. Each query maps
 * endpoints to components and runs @ref hbrick::DagReachability. Trades higher query
 * cost for lower memory use than @ref hbrick::SccDagClosureBaseline.
 */
class SccDagSearchBaseline {
public:
    /**
     * @brief Computes SCCs and builds the condensation DAG for @p graph.
     * @ingroup hbrick_baselines
     *
     * @param graph Input directed graph.
     * @param scratch Workspace for SCC and DAG construction.
     */
    void preprocess(const CsrGraph& graph, GraphSearchScratch& scratch);

    /**
     * @brief Answers reachability by searching the condensation DAG.
     * @ingroup hbrick_baselines
     *
     * @param source Source vertex index in the original graph.
     * @param target Target vertex index in the original graph.
     * @param scratch Reusable traversal workspace for the DAG search.
     * @return Reachability result from component-level search.
     */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. @return The outcome of the most recent @ref preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    SccDecomposition decomposition_;
    CsrGraph condensation_dag_;
};

}  // namespace hbrick
