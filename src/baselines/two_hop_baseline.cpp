#include "hbrick/baselines/two_hop_baseline.hpp"

#include <exception>

#include "hbrick/baselines/baseline_graph_utils.hpp"

namespace hbrick {

namespace {

[[nodiscard]] uint64_t liveLabelStorageBytes(
    const std::vector<std::vector<uint32_t>>& labels_out,
    const std::vector<std::vector<uint32_t>>& labels_in
) noexcept {
    uint64_t total_bytes = 0U;
    constexpr uint64_t kVectorShellBytes = 24U;
    for (const std::vector<uint32_t>& labels : labels_out) {
        total_bytes += kVectorShellBytes;
        total_bytes += static_cast<uint64_t>(labels.capacity()) * sizeof(uint32_t);
    }
    for (const std::vector<uint32_t>& labels : labels_in) {
        total_bytes += kVectorShellBytes;
        total_bytes += static_cast<uint64_t>(labels.capacity()) * sizeof(uint32_t);
    }
    return total_bytes;
}

}  // namespace

uint64_t TwoHopBaseline::estimateMaxLabelBytes(const uint32_t num_vertices) noexcept {
    return 2ULL * static_cast<uint64_t>(num_vertices) * static_cast<uint64_t>(num_vertices)
        * sizeof(uint32_t);
}

void TwoHopBaseline::preprocess(
    const CsrGraph& graph,
    GraphSearchScratch& scratch,
    const uint64_t max_memory_bytes
) {
    status_ = BaselineStatus::NotRun;
    num_vertices_ = 0U;
    skipped_label_storage_bytes_ = 0U;
    labels_out_.clear();
    labels_in_.clear();

    const uint32_t num_vertices = graph.numVertices();
    if (num_vertices == 0U) {
        status_ = BaselineStatus::Completed;
        return;
    }

    try {
        const CsrGraph transpose = buildTransposeGraph(graph);
        labels_out_.assign(num_vertices, {});
        labels_in_.assign(num_vertices, {});

        if (liveLabelStorageBytes(labels_out_, labels_in_) > max_memory_bytes) {
            skipped_label_storage_bytes_ =
                liveLabelStorageBytes(labels_out_, labels_in_);
            labels_out_.clear();
            labels_in_.clear();
            status_ = BaselineStatus::SkippedByPolicy;
            return;
        }

        std::vector<uint32_t> forward_reachable;
        std::vector<uint32_t> backward_reachable;

        for (uint32_t hub = 0U; hub < num_vertices; ++hub) {
            collectForwardReachable(graph, hub, scratch, forward_reachable);
            collectForwardReachable(transpose, hub, scratch, backward_reachable);

            for (const uint32_t vertex : backward_reachable) {
                labels_out_[vertex].push_back(hub);
            }
            for (const uint32_t vertex : forward_reachable) {
                labels_in_[vertex].push_back(hub);
            }

            const uint64_t live_bytes = liveLabelStorageBytes(labels_out_, labels_in_);
            if (live_bytes > max_memory_bytes) {
                skipped_label_storage_bytes_ = live_bytes;
                labels_out_.clear();
                labels_in_.clear();
                status_ = BaselineStatus::SkippedByPolicy;
                return;
            }
        }

        for (std::vector<uint32_t>& labels : labels_out_) {
            sortUniqueLabelsInPlace(labels);
        }
        for (std::vector<uint32_t>& labels : labels_in_) {
            sortUniqueLabelsInPlace(labels);
        }

        num_vertices_ = num_vertices;
        status_ = BaselineStatus::Completed;
    } catch (const std::exception&) {
        labels_out_.clear();
        labels_in_.clear();
        num_vertices_ = 0U;
        status_ = BaselineStatus::Failed;
    }
}

ReachabilityAnswer TwoHopBaseline::query(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source >= num_vertices_ || target >= num_vertices_) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source == target) {
        return ReachabilityAnswer::Reachable;
    }

    return sortedLabelsIntersect(labels_out_[source], labels_in_[target])
        ? ReachabilityAnswer::Reachable
        : ReachabilityAnswer::Unreachable;
}

uint64_t TwoHopBaseline::labelStorageBytes() const noexcept {
    if (status_ == BaselineStatus::Completed) {
        return liveLabelStorageBytes(labels_out_, labels_in_);
    }

    if (status_ == BaselineStatus::SkippedByPolicy) {
        return skipped_label_storage_bytes_;
    }

    return 0U;
}

}  // namespace hbrick
