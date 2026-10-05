#include "hbrick/baselines/scc_dag_closure_baseline.hpp"

#include <exception>
#include <limits>
#include <new>
#include <stdexcept>

#include "hbrick/baselines/closure_matrix_builder.hpp"
#include "hbrick/graph/condensation_graph.hpp"

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

void SccDagClosureBaseline::beginPreprocess(
    const CsrGraph& graph,
    GraphSearchScratch& scratch,
    const uint64_t max_memory_bytes,
    const uint32_t safety_percent,
    const uint32_t max_components
) {
    status_ = BaselineStatus::NotRun;
    decomposition_ = SccDecomposition{};
    num_components_ = 0U;
    next_pivot_ = 0U;
    preprocess_active_ = false;
    condensation_bytes_ = 0U;
    relation_ = BitMatrix{};
    closure_ = BitMatrix{};

    decomposition_ = SccDecomposition::compute(graph, scratch);
    const CondensationGraph condensation = CondensationGraph::fromGraph(graph, decomposition_);
    const uint32_t num_components = condensation.numComponents();

    if (num_components == 0U) {
        status_ = BaselineStatus::Completed;
        return;
    }

    // Keep C visible for reporting even when the closure is skipped.
    num_components_ = num_components;
    condensation_bytes_ = condensation.dag().estimateStorageBytes();

    // Component-count feasibility gate: a large condensation means the C x C
    // bit closure and its cubic pivot schedule are both hopeless, so skip
    // before touching the allocator rather than burning the whole budget.
    if (max_components > 0U && num_components >= max_components) {
        status_ = BaselineStatus::SkippedByPolicy;
        return;
    }

    if (!ClosureMatrixBuilder::canAllocateWorkingSet(
            num_components,
            1U,
            max_memory_bytes,
            safety_percent
        )) {
        status_ = BaselineStatus::SkippedByPolicy;
        return;
    }

    try {
        relation_ = ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(
            condensation.dag(),
            max_memory_bytes
        );
        next_pivot_ = 0U;
        preprocess_active_ = true;
    } catch (const std::bad_alloc&) {
        abortPreprocessOutOfMemory();
    } catch (const std::exception&) {
        status_ = BaselineStatus::Failed;
        relation_ = BitMatrix{};
    }
}

uint64_t SccDagClosureBaseline::estimatedWorkingSetBytes(
    const uint32_t safety_percent
) const noexcept {
    if (num_components_ == 0U) {
        return 0U;
    }
    const uint64_t closure_bytes = ClosureMatrixBuilder::estimateWorkingSetBytes(
        num_components_,
        1U,
        safety_percent
    );
    const uint64_t max_u64 = std::numeric_limits<uint64_t>::max();
    if (closure_bytes > max_u64 - condensation_bytes_) {
        return max_u64;
    }
    return closure_bytes + condensation_bytes_;
}

bool SccDagClosureBaseline::stepPreprocessPivots(const uint32_t pivot_count) noexcept {
    if (!preprocess_active_) {
        return true;
    }

    if (num_components_ == 0U) {
        closure_ = std::move(relation_);
        relation_ = BitMatrix{};
        preprocess_active_ = false;
        status_ = BaselineStatus::Completed;
        return true;
    }

    if (pivot_count == 0U) {
        return false;
    }

    const uint32_t remaining = num_components_ - next_pivot_;
    const uint32_t batch = std::min(pivot_count, remaining);
    for (uint32_t step = 0U; step < batch; ++step) {
        warshallPivot(relation_, next_pivot_);
        ++next_pivot_;
    }

    if (next_pivot_ < num_components_) {
        return false;
    }

    closure_ = std::move(relation_);
    relation_ = BitMatrix{};
    preprocess_active_ = false;
    status_ = BaselineStatus::Completed;
    return true;
}

void SccDagClosureBaseline::abortPreprocessSkippedByPolicy() noexcept {
    relation_ = BitMatrix{};
    closure_ = BitMatrix{};
    decomposition_ = SccDecomposition{};
    next_pivot_ = 0U;
    preprocess_active_ = false;
    status_ = BaselineStatus::SkippedByPolicy;
}

void SccDagClosureBaseline::abortPreprocessOutOfMemory() noexcept {
    relation_ = BitMatrix{};
    closure_ = BitMatrix{};
    decomposition_ = SccDecomposition{};
    next_pivot_ = 0U;
    num_components_ = 0U;
    preprocess_active_ = false;
    status_ = BaselineStatus::OutOfMemory;
}

void SccDagClosureBaseline::preprocess(
    const CsrGraph& graph,
    GraphSearchScratch& scratch,
    const uint64_t max_memory_bytes,
    const uint32_t safety_percent,
    const uint32_t max_components
) {
    beginPreprocess(graph, scratch, max_memory_bytes, safety_percent, max_components);
    if (!preprocess_active_) {
        return;
    }

    while (!stepPreprocessPivots(num_components_)) {
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

uint64_t SccDagClosureBaseline::indexStorageBytes() const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return 0U;
    }

    return closure_.memoryBytes();
}

}  // namespace hbrick
