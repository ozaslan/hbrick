#include "hbrick/baselines/full_closure_baseline.hpp"

#include <exception>

#include "hbrick/baselines/closure_matrix_builder.hpp"
#include "hbrick/bit/boolean_closure.hpp"

namespace hbrick {

namespace {

void warshallPivot(BitMatrix& relation, const uint32_t pivot) noexcept {
    const uint32_t num_vertices = relation.numRows();
    for (uint32_t row_index = 0; row_index < num_vertices; ++row_index) {
        if (relation.test(row_index, pivot)) {
            relation.row(row_index).rowOr(relation.row(pivot));
        }
    }
}

}  // namespace

void FullClosureBaseline::beginPreprocess(
    const CsrGraph& graph,
    const uint64_t max_memory_bytes
) {
    status_ = BaselineStatus::NotRun;
    num_vertices_ = 0;
    next_pivot_ = 0U;
    preprocess_active_ = false;
    relation_ = BitMatrix{};
    closure_ = BitMatrix{};

    const uint32_t num_vertices = graph.numVertices();
    if (!ClosureMatrixBuilder::canAllocateReflexiveAdjacency(num_vertices, max_memory_bytes)) {
        status_ = BaselineStatus::SkippedByPolicy;
        return;
    }

    try {
        relation_ = ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(
            graph,
            max_memory_bytes
        );
        num_vertices_ = num_vertices;
        next_pivot_ = 0U;
        preprocess_active_ = true;
    } catch (const std::exception&) {
        status_ = BaselineStatus::Failed;
        relation_ = BitMatrix{};
    }
}

bool FullClosureBaseline::stepPreprocessPivots(const uint32_t pivot_count) noexcept {
    if (!preprocess_active_ || num_vertices_ == 0U || pivot_count == 0U) {
        return !preprocess_active_;
    }

    const uint32_t remaining = num_vertices_ - next_pivot_;
    const uint32_t batch = std::min(pivot_count, remaining);
    for (uint32_t step = 0U; step < batch; ++step) {
        warshallPivot(relation_, next_pivot_);
        ++next_pivot_;
    }

    if (next_pivot_ < num_vertices_) {
        return false;
    }

    closure_ = std::move(relation_);
    relation_ = BitMatrix{};
    preprocess_active_ = false;
    status_ = BaselineStatus::Completed;
    return true;
}

void FullClosureBaseline::abortPreprocessSkippedByPolicy() noexcept {
    relation_ = BitMatrix{};
    closure_ = BitMatrix{};
    next_pivot_ = 0U;
    preprocess_active_ = false;
    num_vertices_ = 0U;
    status_ = BaselineStatus::SkippedByPolicy;
}

void FullClosureBaseline::preprocess(const CsrGraph& graph, const uint64_t max_memory_bytes) {
    beginPreprocess(graph, max_memory_bytes);
    if (!preprocess_active_) {
        return;
    }

    while (!stepPreprocessPivots(num_vertices_)) {
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

uint64_t FullClosureBaseline::indexStorageBytes() const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return 0U;
    }

    return closure_.memoryBytes();
}

}  // namespace hbrick
