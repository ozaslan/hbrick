/**
 * @file grail_baseline.cpp
 * @brief Implementation of GrailBaseline wrapping the official GRAIL codebase.
 */

#include "hbrick/baselines/grail_baseline.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated"
#endif
#include "Grail.h"
#include "Graph.h"
#include "GraphUtil.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace hbrick {

struct GrailState {
    std::unique_ptr<Graph> g;
    std::unique_ptr<Grail> grail;
    std::vector<int> sccmap;
    uint32_t num_trees = 5U;
    int alg_type = 1;
};

GrailBaseline::GrailBaseline() = default;
GrailBaseline::~GrailBaseline() = default;

GrailBaseline::GrailBaseline(GrailBaseline&&) noexcept = default;
GrailBaseline& GrailBaseline::operator=(GrailBaseline&&) noexcept = default;

uint64_t GrailBaseline::estimateLabelBytes(
    const uint32_t num_vertices,
    const uint32_t num_trees
) noexcept {
    const uint64_t sccmap_bytes = static_cast<uint64_t>(num_vertices) * sizeof(int);
    const uint64_t label_bytes = static_cast<uint64_t>(num_vertices)
                               * static_cast<uint64_t>(num_trees) * 2ULL * sizeof(int);
    const uint64_t worst_dag_bytes = static_cast<uint64_t>(num_vertices) * sizeof(Vertex);
    return sccmap_bytes + label_bytes + worst_dag_bytes;
}

uint64_t GrailBaseline::labelStorageBytes() const noexcept {
    if (status_ != BaselineStatus::Completed || !state_ || !state_->g) {
        return 0ULL;
    }
    const uint64_t sccmap_bytes = static_cast<uint64_t>(state_->sccmap.capacity()) * sizeof(int);
    const uint64_t label_bytes = static_cast<uint64_t>(state_->g->num_vertices())
                               * static_cast<uint64_t>(state_->num_trees) * 2ULL * sizeof(int);
    const uint64_t dag_nodes_bytes = static_cast<uint64_t>(state_->g->num_vertices()) * sizeof(Vertex);
    const uint64_t dag_edges_bytes = static_cast<uint64_t>(state_->g->num_edges()) * sizeof(int);
    return sccmap_bytes + label_bytes + dag_nodes_bytes + dag_edges_bytes;
}

void GrailBaseline::preprocess(
    const CsrGraph& graph,
    const GrailBaselineParams& params,
    const uint64_t max_memory_bytes
) {
    status_ = BaselineStatus::NotRun;
    num_vertices_ = 0U;
    state_.reset();

    const uint32_t num_vertices = graph.numVertices();
    if (num_vertices == 0U) {
        status_ = BaselineStatus::Completed;
        return;
    }

    if (estimateLabelBytes(num_vertices, params.num_trees) > max_memory_bytes) {
        status_ = BaselineStatus::SkippedByPolicy;
        return;
    }

    try {
        num_vertices_ = num_vertices;
        auto state = std::make_unique<GrailState>();
        state->num_trees = params.num_trees;
        state->alg_type = params.alg_type;
        state->sccmap.resize(num_vertices);

        // Convert CsrGraph to Grail Graph
        state->g = std::make_unique<Graph>(static_cast<int>(num_vertices));
        for (uint32_t u = 0U; u < num_vertices; ++u) {
            for (const uint32_t v : graph.outNeighbors(u)) {
                if (u != v) {
                    state->g->addEdge(static_cast<int>(u), static_cast<int>(v));
                }
            }
        }

        // Merge SCCs into condensation DAG
        std::vector<int> reverse_topo_sort;
        GraphUtil::mergeSCC(*(state->g), state->sccmap.data(), reverse_topo_sort);
        GraphUtil::topo_leveler(*(state->g));

        // Build GRAIL indexing
        state->grail = std::make_unique<Grail>(
            *(state->g),
            static_cast<int>(params.num_trees),
            params.labeling_type,
            false,
            static_cast<int>(params.num_trees)
        );

        const bool use_level_filter = (params.alg_type == 2 || params.alg_type == 6
                                    || params.alg_type == -2 || params.alg_type == -6);
        state->grail->set_level_filter(use_level_filter);

        state_ = std::move(state);
        status_ = BaselineStatus::Completed;
    } catch (const std::bad_alloc&) {
        state_.reset();
        status_ = BaselineStatus::OutOfMemory;
    } catch (...) {
        state_.reset();
        status_ = BaselineStatus::Failed;
    }
}

bool GrailBaseline::intervalLabelsContain(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    if (status_ != BaselineStatus::Completed || !state_ || !state_->grail) {
        return false;
    }
    if (source >= num_vertices_ || target >= num_vertices_) {
        return false;
    }
    const int s = state_->sccmap[source];
    const int t = state_->sccmap[target];
    if (s == t) {
        return true;
    }
    return state_->grail->contains(s, t);
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
    (void)scratch;
    if (status_ != BaselineStatus::Completed || !state_ || !state_->grail) {
        return GrailQueryOutcome{ReachabilityAnswer::Unreachable, false};
    }
    if (source >= num_vertices_ || target >= num_vertices_) {
        return GrailQueryOutcome{ReachabilityAnswer::Unreachable, false};
    }
    if (source == target) {
        return GrailQueryOutcome{ReachabilityAnswer::Reachable, true};
    }
    const int s = state_->sccmap[source];
    const int t = state_->sccmap[target];
    if (s == t) {
        return GrailQueryOutcome{ReachabilityAnswer::Reachable, true};
    }
    if (!state_->grail->contains(s, t)) {
        return GrailQueryOutcome{ReachabilityAnswer::Unreachable, true};
    }

    bool reached = false;
    switch (state_->alg_type) {
        case 1:  reached = state_->grail->reach(s, t, nullptr); break;
        case 2:  reached = state_->grail->reach_lf(s, t, nullptr); break;
        case 3:  reached = state_->grail->bidirectionalReach(s, t, nullptr); break;
        case 6:  reached = state_->grail->bidirectionalReach_lf(s, t, nullptr); break;
        case -1: reached = state_->grail->reachPP(s, t, nullptr); break;
        case -2: reached = state_->grail->reachPP_lf(s, t, nullptr); break;
        case -3: reached = state_->grail->bidirectionalReachPP(s, t, nullptr); break;
        case -6: reached = state_->grail->bidirectionalReachPP_lf(s, t, nullptr); break;
        default: reached = state_->grail->reach(s, t, nullptr); break;
    }

    return GrailQueryOutcome{
        reached ? ReachabilityAnswer::Reachable : ReachabilityAnswer::Unreachable,
        false
    };
}

}  // namespace hbrick
