#include "hbrick/baselines/scc_dag_search_baseline.hpp"

#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/dag_reachability.hpp"
#include "hbrick/graph/edge32.hpp"

namespace hbrick {

namespace {

struct DfsFrame {
    uint32_t vertex = 0U;
    uint32_t next_neighbor_index = 0U;
};

[[nodiscard]] CsrGraph buildTransposeGraph(const CsrGraph& graph) {
    CsrGraphBuilder builder{graph.numVertices()};
    for (const Edge32& edge : graph.edges()) {
        builder.addEdge(edge.to, edge.from);
    }
    return builder.build();
}

void runIterativeDfs(
    const CsrGraph& graph,
    const uint32_t start,
    const uint32_t mark,
    std::vector<uint32_t>& visited,
    std::vector<uint32_t>& finish_order,
    std::vector<DfsFrame>& frames
) {
    frames.clear();
    visited[start] = mark;
    frames.push_back(DfsFrame{start, 0U});

    while (!frames.empty()) {
        DfsFrame& frame = frames.back();
        const std::span<const uint32_t> neighbors = graph.outNeighbors(frame.vertex);

        if (frame.next_neighbor_index < neighbors.size()) {
            const uint32_t neighbor = neighbors[frame.next_neighbor_index];
            ++frame.next_neighbor_index;

            if (visited[neighbor] != mark) {
                visited[neighbor] = mark;
                frames.push_back(DfsFrame{neighbor, 0U});
            }
        } else {
            finish_order.push_back(frame.vertex);
            frames.pop_back();
        }
    }
}

}  // namespace

void SccDagSearchBaseline::resetPreprocessState() noexcept {
    preprocess_phase_ = PreprocessPhase::Idle;
    preprocess_graph_ = nullptr;
    first_pass_cursor_ = 0U;
    second_pass_cursor_ = 0U;
    condensation_cursor_ = 0U;
    visit_mark_ = 0U;
    finish_order_.clear();
    component_ids_.clear();
    transpose_graph_ = CsrGraph{};
    condensation_builder_ = CsrGraphBuilder{0U};
    num_components_ = 0U;
    preprocess_work_completed_ = 0U;
    preprocess_work_total_ = 0U;
}

void SccDagSearchBaseline::beginPreprocess(
    const CsrGraph& graph,
    GraphSearchScratch& scratch
) {
    status_ = BaselineStatus::NotRun;
    decomposition_ = SccDecomposition{};
    condensation_dag_ = CsrGraph{};
    resetPreprocessState();

    preprocess_graph_ = &graph;
    scratch.resetForGraph(graph.numVertices());
    visit_mark_ = scratch.nextMark();

    finish_order_.clear();
    finish_order_.reserve(graph.numVertices());
    component_ids_.assign(graph.numVertices(), 0U);

  preprocess_work_total_ =
        static_cast<uint64_t>(graph.numVertices()) * 3ULL + 2ULL;
    preprocess_phase_ = PreprocessPhase::FirstPass;
    first_pass_cursor_ = 0U;
}

void SccDagSearchBaseline::runFirstPassBatch(
    GraphSearchScratch& scratch,
    const uint32_t vertex_batch
) noexcept {
    if (preprocess_graph_ == nullptr) {
        return;
    }

    const CsrGraph& graph = *preprocess_graph_;
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<DfsFrame> frames;
    frames.reserve(graph.numVertices());

    uint32_t processed = 0U;
    while (first_pass_cursor_ < graph.numVertices() && processed < vertex_batch) {
        if (visited[first_pass_cursor_] != visit_mark_) {
            runIterativeDfs(
                graph,
                first_pass_cursor_,
                visit_mark_,
                visited,
                finish_order_,
                frames
            );
        }
        ++first_pass_cursor_;
        ++processed;
        ++preprocess_work_completed_;
    }

    if (first_pass_cursor_ >= graph.numVertices()) {
        preprocess_phase_ = PreprocessPhase::Transpose;
    }
}

void SccDagSearchBaseline::runSecondPassBatch(
    GraphSearchScratch& scratch,
    const uint32_t vertex_batch
) noexcept {
    if (preprocess_graph_ == nullptr) {
        return;
    }

    const CsrGraph& transpose_graph = transpose_graph_;
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<uint32_t>& stack = scratch.stack();

    uint32_t processed = 0U;
    while (second_pass_cursor_ > 0U && processed < vertex_batch) {
        --second_pass_cursor_;
        const uint32_t start = finish_order_[second_pass_cursor_];
        if (visited[start] == visit_mark_) {
            ++processed;
            ++preprocess_work_completed_;
            continue;
        }

        stack.clear();
        stack.push_back(start);
        visited[start] = visit_mark_;

        while (!stack.empty()) {
            const uint32_t vertex = stack.back();
            stack.pop_back();
            component_ids_[vertex] = num_components_;

            for (const uint32_t neighbor : transpose_graph.outNeighbors(vertex)) {
                if (visited[neighbor] == visit_mark_) {
                    continue;
                }
                visited[neighbor] = visit_mark_;
                stack.push_back(neighbor);
            }
        }

        ++num_components_;
        ++processed;
        ++preprocess_work_completed_;
    }

    if (second_pass_cursor_ == 0U) {
        decomposition_ = SccDecomposition::fromComponentLabels(
            preprocess_graph_->numVertices(),
            num_components_,
            std::move(component_ids_)
        );
        condensation_builder_ =
            CsrGraphBuilder{decomposition_.numComponents()};
        condensation_cursor_ = 0U;
        preprocess_phase_ = PreprocessPhase::Condensation;
    }
}

void SccDagSearchBaseline::runCondensationBatch(const uint32_t vertex_batch) noexcept {
    if (preprocess_graph_ == nullptr) {
        return;
    }

    const CsrGraph& graph = *preprocess_graph_;
    uint32_t processed = 0U;
    while (condensation_cursor_ < graph.numVertices() && processed < vertex_batch) {
        for (const uint32_t to : graph.outNeighbors(condensation_cursor_)) {
            const uint32_t source_component =
                decomposition_.componentOf(condensation_cursor_);
            const uint32_t target_component = decomposition_.componentOf(to);
            if (source_component != target_component) {
                condensation_builder_.addEdge(source_component, target_component);
            }
        }
        ++condensation_cursor_;
        ++processed;
        ++preprocess_work_completed_;
    }

    if (condensation_cursor_ >= graph.numVertices()) {
        condensation_dag_ = condensation_builder_.build();
        status_ = BaselineStatus::Completed;
        preprocess_phase_ = PreprocessPhase::Done;
    }
}

bool SccDagSearchBaseline::preprocessStep(
    GraphSearchScratch& scratch,
    const uint32_t vertex_batch
) noexcept {
    const uint32_t batch = std::max(1U, vertex_batch);

    switch (preprocess_phase_) {
        case PreprocessPhase::Idle:
        case PreprocessPhase::Done:
            return true;
        case PreprocessPhase::FirstPass:
            runFirstPassBatch(scratch, batch);
            return preprocess_phase_ == PreprocessPhase::Done;
        case PreprocessPhase::Transpose:
            transpose_graph_ = buildTransposeGraph(*preprocess_graph_);
            visit_mark_ = scratch.nextMark();
            second_pass_cursor_ = finish_order_.size();
            num_components_ = 0U;
            preprocess_phase_ = PreprocessPhase::SecondPass;
            ++preprocess_work_completed_;
            return false;
        case PreprocessPhase::SecondPass:
            runSecondPassBatch(scratch, batch);
            return preprocess_phase_ == PreprocessPhase::Done;
        case PreprocessPhase::Condensation:
            runCondensationBatch(batch);
            return preprocess_phase_ == PreprocessPhase::Done;
    }

    return true;
}

void SccDagSearchBaseline::cancelPreprocess() noexcept {
    resetPreprocessState();
    status_ = BaselineStatus::NotRun;
}

void SccDagSearchBaseline::preprocess(
    const CsrGraph& graph,
    GraphSearchScratch& scratch
) {
    beginPreprocess(graph, scratch);
    while (!preprocessStep(scratch, graph.numVertices())) {
    }
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
