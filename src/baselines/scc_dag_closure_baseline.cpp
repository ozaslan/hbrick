#include "hbrick/baselines/scc_dag_closure_baseline.hpp"

#include <exception>

#include "hbrick/baselines/closure_matrix_builder.hpp"
#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/condensation_graph.hpp"

namespace hbrick {

void SccDagClosureBaseline::preprocess(
    const CsrGraph& graph,
    GraphSearchScratch& scratch,
    const uint64_t max_memory_bytes
) {
    status_ = BaselineStatus::NotRun;
    decomposition_ = SccDecomposition{};
    closure_ = BitMatrix{};

    decomposition_ = SccDecomposition::compute(graph, scratch);
    const CondensationGraph condensation = CondensationGraph::fromGraph(graph, decomposition_);
    const uint32_t num_components = condensation.numComponents();

    if (!ClosureMatrixBuilder::canAllocateReflexiveAdjacency(num_components, max_memory_bytes)) {
        status_ = BaselineStatus::SkippedByPolicy;
        return;
    }

    try {
        BitMatrix relation = ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(
            condensation.dag(),
            max_memory_bytes
        );
        closure_ = BooleanClosure::transitiveClosureWarshall(std::move(relation));
        status_ = BaselineStatus::Completed;
    } catch (const std::exception&) {
        status_ = BaselineStatus::Failed;
    }
}

ReachabilityAnswer SccDagClosureBaseline::query(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source >= decomposition_.numVertices() || target >= decomposition_.numVertices()) {
        return ReachabilityAnswer::Unreachable;
    }

    const uint32_t source_component = decomposition_.componentOf(source);
    const uint32_t target_component = decomposition_.componentOf(target);

    return closure_.test(source_component, target_component)
        ? ReachabilityAnswer::Reachable
        : ReachabilityAnswer::Unreachable;
}

}  // namespace hbrick
