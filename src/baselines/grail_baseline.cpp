#include "hbrick/baselines/grail_baseline.hpp"

#include <algorithm>
#include <exception>
#include <numeric>
#include <random>
#include <vector>

#include "hbrick/graph/bfs.hpp"

namespace hbrick {

namespace {

void buildDfsLabeling(
    const CsrGraph& graph,
    const std::vector<uint32_t>& permutation,
    std::vector<uint32_t>& pre,
    std::vector<uint32_t>& post
) {
    const uint32_t num_vertices = graph.numVertices();
    pre.assign(num_vertices, 0U);
    post.assign(num_vertices, 0U);

    std::vector<uint8_t> visited(num_vertices, 0U);

    uint32_t timer = 0U;

    const auto dfs = [&](const auto& self, const uint32_t vertex) -> void {
        pre[vertex] = timer++;

        for (const uint32_t neighbor : graph.outNeighbors(vertex)) {
            if (visited[neighbor] != 0U) {
                continue;
            }

            visited[neighbor] = 1U;
            self(self, neighbor);
        }

        post[vertex] = timer++;
    };

    for (const uint32_t vertex : permutation) {
        if (visited[vertex] != 0U) {
            continue;
        }

        visited[vertex] = 1U;
        dfs(dfs, vertex);
    }
}

}  // namespace

uint64_t GrailBaseline::estimateLabelBytes(
    const uint32_t num_vertices,
    const uint32_t num_trees
) noexcept {
    return 2ULL * static_cast<uint64_t>(num_vertices) * static_cast<uint64_t>(num_trees)
        * sizeof(uint32_t);
}

bool GrailBaseline::intervalContains(
    const TreeLabeling& labeling,
    const uint32_t source,
    const uint32_t target
) noexcept {
    if (source >= labeling.pre.size() || target >= labeling.pre.size()) {
        return false;
    }

    const uint32_t source_pre = labeling.pre[source];
    const uint32_t source_post = labeling.post[source];
    const uint32_t target_pre = labeling.pre[target];
    const uint32_t target_post = labeling.post[target];

    return source_pre <= target_pre && target_post <= source_post;
}

void GrailBaseline::preprocess(
    const CsrGraph& graph,
    const GrailBaselineParams& params,
    const uint64_t max_memory_bytes
) {
    status_ = BaselineStatus::NotRun;
    graph_ = CsrGraph{};
    labelings_.clear();

    const uint32_t num_vertices = graph.numVertices();
    if (num_vertices == 0U) {
        graph_ = graph;
        status_ = BaselineStatus::Completed;
        return;
    }

    if (params.num_trees == 0U) {
        status_ = BaselineStatus::Failed;
        return;
    }

    if (estimateLabelBytes(num_vertices, params.num_trees) > max_memory_bytes) {
        status_ = BaselineStatus::SkippedByPolicy;
        return;
    }

    try {
        graph_ = graph;
        labelings_.resize(params.num_trees);

        std::vector<uint32_t> permutation(num_vertices);
        std::iota(permutation.begin(), permutation.end(), 0U);

        for (uint32_t tree_index = 0U; tree_index < params.num_trees; ++tree_index) {
            std::mt19937_64 rng(params.seed + static_cast<uint64_t>(tree_index) + 1ULL);
            std::shuffle(permutation.begin(), permutation.end(), rng);
            buildDfsLabeling(
                graph_,
                permutation,
                labelings_[tree_index].pre,
                labelings_[tree_index].post
            );
        }

        status_ = BaselineStatus::Completed;
    } catch (const std::exception&) {
        graph_ = CsrGraph{};
        labelings_.clear();
        status_ = BaselineStatus::Failed;
    }
}

ReachabilityAnswer GrailBaseline::query(
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) const noexcept {
    return queryDetailed(source, target, scratch).answer;
}

GrailQueryOutcome GrailBaseline::queryDetailed(
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) const noexcept {
    GrailQueryOutcome outcome;
    if (status_ != BaselineStatus::Completed) {
        return outcome;
    }

    const uint32_t num_vertices = graph_.numVertices();
    if (source >= num_vertices || target >= num_vertices) {
        return outcome;
    }

    if (source == target) {
        outcome.answer = ReachabilityAnswer::Reachable;
        outcome.tree_certified = true;
        return outcome;
    }

    for (const TreeLabeling& labeling : labelings_) {
        if (intervalContains(labeling, source, target)) {
            outcome.answer = ReachabilityAnswer::Reachable;
            outcome.tree_certified = true;
            return outcome;
        }
    }

    outcome.answer = Bfs::reachable(graph_, source, target, scratch);
    outcome.tree_certified = false;
    return outcome;
}

uint64_t GrailBaseline::labelStorageBytes() const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return 0U;
    }

    uint64_t total_bytes = 0U;
    for (const TreeLabeling& labeling : labelings_) {
        total_bytes += static_cast<uint64_t>(labeling.pre.size()) * sizeof(uint32_t);
        total_bytes += static_cast<uint64_t>(labeling.post.size()) * sizeof(uint32_t);
    }
    return total_bytes;
}

}  // namespace hbrick
