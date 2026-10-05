#include "hbrick/baselines/full_closure_baseline.hpp"

#include <exception>
#include <new>
#include <stdexcept>

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
    const uint64_t max_memory_bytes,
    const uint32_t safety_percent
) {
    status_ = BaselineStatus::NotRun;
    num_vertices_ = 0;
    next_pivot_ = 0U;
    preprocess_active_ = false;
    graph_ = CsrGraph{};
    relation_ = BitMatrix{};
    multiply_scratch_ = BitMatrix{};
    closure_ = BitMatrix{};

    const uint32_t num_vertices = graph.numVertices();
    if (num_vertices == 0U) {
        status_ = BaselineStatus::Completed;
        return;
    }

    if (!ClosureMatrixBuilder::canAllocateWorkingSet(
            num_vertices,
            peakMatrixCount(),
            max_memory_bytes,
            safety_percent
        )) {
        status_ = BaselineStatus::SkippedByPolicy;
        return;
    }

    try {
        relation_ = ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(
            graph,
            max_memory_bytes
        );
        if (kernel_ == FullClosureKernel::SccCompressedKleene) {
            graph_ = graph;
        }
        num_vertices_ = num_vertices;
        next_pivot_ = 0U;
        preprocess_active_ = true;
    } catch (const std::bad_alloc&) {
        abortPreprocessOutOfMemory();
    } catch (const std::exception&) {
        status_ = BaselineStatus::Failed;
        graph_ = CsrGraph{};
        relation_ = BitMatrix{};
    }
}

uint32_t FullClosureBaseline::peakMatrixCount() const noexcept {
    return kernel_ == FullClosureKernel::SccCompressedKleene ? 2U : 1U;
}

uint64_t FullClosureBaseline::estimatedWorkingSetBytes(
    const uint32_t safety_percent
) const noexcept {
    if (num_vertices_ == 0U) {
        return 0U;
    }
    return ClosureMatrixBuilder::estimateWorkingSetBytes(
        num_vertices_,
        peakMatrixCount(),
        safety_percent
    );
}

bool FullClosureBaseline::stepPreprocessPivots(const uint32_t pivot_count) noexcept {
    if (!preprocess_active_) {
        return true;
    }

    if (num_vertices_ == 0U) {
        return finishPreprocess();
    }

    if (kernel_ == FullClosureKernel::SccCompressedKleene) {
        try {
            ClosureMatrixBuilder::transitiveClosureKleeneTruncatedInPlace(
                relation_,
                graph_,
                &multiply_scratch_
            );
        } catch (const std::bad_alloc&) {
            abortPreprocessOutOfMemory();
            return true;
        } catch (const std::exception&) {
            abortPreprocessFailed();
            return true;
        }

        next_pivot_ = num_vertices_;
        return finishPreprocess();
    }

    if (pivot_count == 0U) {
        return false;
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

    return finishPreprocess();
}

bool FullClosureBaseline::finishPreprocess() noexcept {
    closure_ = std::move(relation_);
    graph_ = CsrGraph{};
    relation_ = BitMatrix{};
    multiply_scratch_ = BitMatrix{};
    preprocess_active_ = false;
    status_ = BaselineStatus::Completed;
    return true;
}

void FullClosureBaseline::abortPreprocessFailed() noexcept {
    graph_ = CsrGraph{};
    relation_ = BitMatrix{};
    multiply_scratch_ = BitMatrix{};
    closure_ = BitMatrix{};
    preprocess_active_ = false;
    status_ = BaselineStatus::Failed;
}

void FullClosureBaseline::abortPreprocessOutOfMemory() noexcept {
    graph_ = CsrGraph{};
    relation_ = BitMatrix{};
    multiply_scratch_ = BitMatrix{};
    closure_ = BitMatrix{};
    next_pivot_ = 0U;
    preprocess_active_ = false;
    num_vertices_ = 0U;
    status_ = BaselineStatus::OutOfMemory;
}

void FullClosureBaseline::abortPreprocessSkippedByPolicy() noexcept {
    graph_ = CsrGraph{};
    relation_ = BitMatrix{};
    multiply_scratch_ = BitMatrix{};
    closure_ = BitMatrix{};
    next_pivot_ = 0U;
    preprocess_active_ = false;
    num_vertices_ = 0U;
    status_ = BaselineStatus::SkippedByPolicy;
}

void FullClosureBaseline::preprocess(
    const CsrGraph& graph,
    const uint64_t max_memory_bytes,
    const uint32_t safety_percent
) {
    beginPreprocess(graph, max_memory_bytes, safety_percent);
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
