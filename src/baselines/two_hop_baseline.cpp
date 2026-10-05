#include "hbrick/baselines/two_hop_baseline.hpp"

#include <chrono>
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
    const uint64_t max_memory_bytes,
    const uint32_t timeout_seconds
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
        const auto t_start = std::chrono::steady_clock::now();
        const auto max_dur = std::chrono::seconds(timeout_seconds);
        auto is_timed_out = [&]() noexcept -> bool {
            return timeout_seconds > 0U && (std::chrono::steady_clock::now() - t_start > max_dur);
        };

        if (is_timed_out()) {
            status_ = BaselineStatus::SkippedByPolicy;
            return;
        }

        // Timeout granularity is per hub, per 1024 BFS steps, and per label
        // vector; the transpose pass and a single label sort are not
        // interruptible, which bounds any timeout overshoot by one such pass.
        const CsrGraph transpose = graph.transpose();

        // The CSR transpose is allocated by this routine, so it must be charged
        // against the caller-provided budget before label accounting starts.
        const uint64_t transpose_bytes =
            static_cast<uint64_t>(transpose.numVertices() + 1U) * sizeof(uint32_t)
            + transpose.numEdges() * sizeof(uint32_t);
        const uint64_t label_budget_bytes =
            (max_memory_bytes > transpose_bytes) ? (max_memory_bytes - transpose_bytes) : 0U;

        if (is_timed_out()) {
            status_ = BaselineStatus::SkippedByPolicy;
            return;
        }

        labels_out_.assign(num_vertices, {});
        labels_in_.assign(num_vertices, {});

        if (liveLabelStorageBytes(labels_out_, labels_in_) > label_budget_bytes) {
            skipped_label_storage_bytes_ =
                liveLabelStorageBytes(labels_out_, labels_in_);
            labels_out_.clear();
            labels_in_.clear();
            status_ = BaselineStatus::SkippedByPolicy;
            return;
        }

        uint64_t live_capacity_bytes = liveLabelStorageBytes(labels_out_, labels_in_);
        std::vector<uint32_t> forward_reachable;
        std::vector<uint32_t> backward_reachable;

        for (uint32_t hub = 0U; hub < num_vertices; ++hub) {
            if (is_timed_out()) {
                skipped_label_storage_bytes_ = live_capacity_bytes;
                labels_out_.clear();
                labels_in_.clear();
                status_ = BaselineStatus::SkippedByPolicy;
                return;
            }
            if (!collectForwardReachableTimed(graph, hub, scratch, forward_reachable, is_timed_out)) {
                skipped_label_storage_bytes_ = live_capacity_bytes;
                labels_out_.clear();
                labels_in_.clear();
                status_ = BaselineStatus::SkippedByPolicy;
                return;
            }
            if (!collectForwardReachableTimed(transpose, hub, scratch, backward_reachable, is_timed_out)) {
                skipped_label_storage_bytes_ = live_capacity_bytes;
                labels_out_.clear();
                labels_in_.clear();
                status_ = BaselineStatus::SkippedByPolicy;
                return;
            }

            uint32_t back_step = 0U;
            for (const uint32_t vertex : backward_reachable) {
                if ((++back_step & 1023U) == 0U && is_timed_out()) {
                    skipped_label_storage_bytes_ = live_capacity_bytes;
                    labels_out_.clear();
                    labels_in_.clear();
                    status_ = BaselineStatus::SkippedByPolicy;
                    return;
                }
                if (labels_out_[vertex].size() == labels_out_[vertex].capacity()) {
                    const std::size_t cur_cap = labels_out_[vertex].capacity();
                    const std::size_t next_cap = (cur_cap == 0U) ? 1U : (cur_cap * 2U);
                    const uint64_t added_bytes = static_cast<uint64_t>(next_cap - cur_cap) * sizeof(uint32_t);
                    if (live_capacity_bytes + added_bytes > label_budget_bytes) {
                        skipped_label_storage_bytes_ = live_capacity_bytes + added_bytes;
                        labels_out_.clear();
                        labels_in_.clear();
                        status_ = BaselineStatus::SkippedByPolicy;
                        return;
                    }
                    live_capacity_bytes += added_bytes;
                }
                labels_out_[vertex].push_back(hub);
            }
            uint32_t fwd_step = 0U;
            for (const uint32_t vertex : forward_reachable) {
                if ((++fwd_step & 1023U) == 0U && is_timed_out()) {
                    skipped_label_storage_bytes_ = live_capacity_bytes;
                    labels_out_.clear();
                    labels_in_.clear();
                    status_ = BaselineStatus::SkippedByPolicy;
                    return;
                }
                if (labels_in_[vertex].size() == labels_in_[vertex].capacity()) {
                    const std::size_t cur_cap = labels_in_[vertex].capacity();
                    const std::size_t next_cap = (cur_cap == 0U) ? 1U : (cur_cap * 2U);
                    const uint64_t added_bytes = static_cast<uint64_t>(next_cap - cur_cap) * sizeof(uint32_t);
                    if (live_capacity_bytes + added_bytes > label_budget_bytes) {
                        skipped_label_storage_bytes_ = live_capacity_bytes + added_bytes;
                        labels_out_.clear();
                        labels_in_.clear();
                        status_ = BaselineStatus::SkippedByPolicy;
                        return;
                    }
                    live_capacity_bytes += added_bytes;
                }
                labels_in_[vertex].push_back(hub);
            }
        }

        for (std::vector<uint32_t>& labels : labels_out_) {
            if (is_timed_out()) {
                skipped_label_storage_bytes_ = live_capacity_bytes;
                labels_out_.clear();
                labels_in_.clear();
                status_ = BaselineStatus::SkippedByPolicy;
                return;
            }
            sortUniqueLabelsInPlace(labels);
        }
        for (std::vector<uint32_t>& labels : labels_in_) {
            if (is_timed_out()) {
                skipped_label_storage_bytes_ = live_capacity_bytes;
                labels_out_.clear();
                labels_in_.clear();
                status_ = BaselineStatus::SkippedByPolicy;
                return;
            }
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
