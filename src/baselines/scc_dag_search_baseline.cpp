#include "hbrick/baselines/scc_dag_search_baseline.hpp"

#include "hbrick/graph/condensation_graph.hpp"
#include "hbrick/graph/dag_reachability.hpp"

namespace hbrick {

void SccDagSearchBaseline::preprocess(const CsrGraph& graph, GraphSearchScratch& scratch) {
    status_ = BaselineStatus::NotRun;
    decomposition_ = SccDecomposition{};
    condensation_dag_ = CsrGraph{};

    decomposition_ = SccDecomposition::compute(graph, scratch);
    const CondensationGraph condensation = CondensationGraph::fromGraph(graph, decomposition_);
    condensation_dag_ = condensation.dag();
    status_ = BaselineStatus::Completed;
}

ReachabilityAnswer SccDagSearchBaseline::query(
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source >= decomposition_.numVertices() || target >= decomposition_.numVertices()) {
        return ReachabilityAnswer::Unreachable;
    }

    const uint32_t source_component = decomposition_.componentOf(source);
    const uint32_t target_component = decomposition_.componentOf(target);

    return DagReachability::reachable(
        condensation_dag_,
        source_component,
        target_component,
        scratch
    );
}

}  // namespace hbrick
