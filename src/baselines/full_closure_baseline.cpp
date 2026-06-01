#include "hbrick/baselines/full_closure_baseline.hpp"

#include <exception>

#include "hbrick/baselines/closure_matrix_builder.hpp"
#include "hbrick/bit/boolean_closure.hpp"

namespace hbrick {

void FullClosureBaseline::preprocess(const CsrGraph& graph, const uint64_t max_memory_bytes) {
    status_ = BaselineStatus::NotRun;
    num_vertices_ = 0;
    closure_ = BitMatrix{};

    const uint32_t num_vertices = graph.numVertices();
    if (!ClosureMatrixBuilder::canAllocateReflexiveAdjacency(num_vertices, max_memory_bytes)) {
        status_ = BaselineStatus::SkippedByPolicy;
        return;
    }

    try {
        BitMatrix relation = ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(
            graph,
            max_memory_bytes
        );
        closure_ = BooleanClosure::transitiveClosureWarshall(std::move(relation));
        num_vertices_ = num_vertices;
        status_ = BaselineStatus::Completed;
    } catch (const std::exception&) {
        status_ = BaselineStatus::Failed;
    }
}

ReachabilityAnswer FullClosureBaseline::query(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source >= num_vertices_ || target >= num_vertices_) {
        return ReachabilityAnswer::Unreachable;
    }

    return closure_.test(source, target) ? ReachabilityAnswer::Reachable
                                         : ReachabilityAnswer::Unreachable;
}

}  // namespace hbrick
