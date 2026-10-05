#include "hbrick/baselines/oreach_baseline.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <vector>

#include "app/reachabilityconfig.h"
#include "extern/KaHIP/lib/data_structure/graph_access.h"
#include "extern/KaHIP/lib/definitions.h"
#include "hbrick/graph/condensation_graph.hpp"
#include "hbrick/graph/scc_decomposition.hpp"
#include "lib/reachability/Query.h"
#include "lib/reachability/oreach.h"

namespace hbrick {

struct OreachState {
    ReachabilityConfig config;
    graph_access condensation_graph;
    std::unique_ptr<oreach> index;
    std::vector<uint32_t> component_of;
    uint32_t num_components = 0U;
};

OreachBaseline::OreachBaseline() = default;
OreachBaseline::~OreachBaseline() = default;

OreachBaseline::OreachBaseline(OreachBaseline&&) noexcept = default;
OreachBaseline& OreachBaseline::operator=(OreachBaseline&&) noexcept = default;

uint64_t OreachBaseline::estimateIndexBytes(
    const uint32_t num_vertices,
    const uint64_t num_edges
) noexcept {
    const uint64_t component_map_bytes =
        static_cast<uint64_t>(num_vertices) * sizeof(uint32_t);
    const uint64_t node_info_bytes =
        static_cast<uint64_t>(num_vertices) * sizeof(NodeInfo);
    const uint64_t graph_bytes =
        static_cast<uint64_t>(num_vertices + 1U) * sizeof(Node)
        + num_edges * sizeof(Edge);
    const uint64_t scratch_bytes =
        static_cast<uint64_t>(num_vertices) * (2 * sizeof(char) + 2 * sizeof(NodeID) + 2 * sizeof(NodeID));

    return component_map_bytes + node_info_bytes + graph_bytes + scratch_bytes;
}

uint64_t OreachBaseline::indexStorageBytes() const noexcept {
    if (status_ != BaselineStatus::Completed || !state_ || !state_->index) {
        return 0ULL;
    }
    const uint64_t component_map_bytes =
        static_cast<uint64_t>(state_->component_of.capacity()) * sizeof(uint32_t);
    const uint64_t node_info_bytes =
        static_cast<uint64_t>(state_->num_components) * sizeof(NodeInfo);
    const uint64_t graph_nodes_bytes =
        static_cast<uint64_t>(state_->condensation_graph.number_of_nodes() + 1U) * sizeof(Node);
    const uint64_t graph_edges_bytes =
        static_cast<uint64_t>(state_->condensation_graph.number_of_edges()) * sizeof(Edge);
    return component_map_bytes + node_info_bytes + graph_nodes_bytes + graph_edges_bytes;
}

void OreachBaseline::preprocess(
    const CsrGraph& graph,
    const OreachBaselineParams& params,
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

    if (estimateIndexBytes(num_vertices, graph.numEdges()) > max_memory_bytes) {
        status_ = BaselineStatus::SkippedByPolicy;
        return;
    }

    try {
        GraphSearchScratch scratch(num_vertices);
        const SccDecomposition decomposition = SccDecomposition::compute(graph, scratch);
        const CondensationGraph condensation =
            CondensationGraph::fromGraph(graph, decomposition);

        num_vertices_ = num_vertices;
        const uint32_t num_components = condensation.numComponents();
        const CsrGraph& dag = condensation.dag();
        const uint64_t num_dag_edges = dag.numEdges();

        auto state = std::make_unique<OreachState>();
        state->num_components = num_components;
        state->component_of.resize(num_vertices);
        for (uint32_t vertex = 0U; vertex < num_vertices; ++vertex) {
            state->component_of[vertex] = condensation.componentOf(vertex);
        }

        state->config.seed = static_cast<MersenneTwister::result_type>(params.seed);
        state->config.l = params.l;
        state->config.pruning_bibfs = params.pruning_bibfs;
        state->config.pruning_dfs = params.pruning_dfs;

        state->condensation_graph.start_construction(
            static_cast<NodeID>(num_components),
            static_cast<EdgeID>(num_dag_edges)
        );
        for (uint32_t c = 0U; c < num_components; ++c) {
            state->condensation_graph.new_node();
            for (const uint32_t succ : dag.outNeighbors(c)) {
                state->condensation_graph.new_edge(c, succ);
            }
        }
        state->condensation_graph.finish_construction();

        state->index = std::make_unique<oreach>(state->condensation_graph, state->config);
        state->index->initialize();

        state_ = std::move(state);
        status_ = BaselineStatus::Completed;
    } catch (const std::exception&) {
        num_vertices_ = 0U;
        state_.reset();
        status_ = BaselineStatus::Failed;
    }
}

ReachabilityAnswer OreachBaseline::query(
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) const noexcept {
    return queryDetailed(source, target, scratch).answer;
}

OreachQueryOutcome OreachBaseline::queryDetailed(
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& /* scratch */
) const noexcept {
    if (status_ != BaselineStatus::Completed || !state_ || !state_->index) {
        return {ReachabilityAnswer::Unreachable, false};
    }
    if (source >= num_vertices_ || target >= num_vertices_) {
        return {ReachabilityAnswer::Unreachable, false};
    }
    if (source == target) {
        return {ReachabilityAnswer::Reachable, true};
    }

    const uint32_t source_comp = state_->component_of[source];
    const uint32_t target_comp = state_->component_of[target];

    if (source_comp == target_comp) {
        return {ReachabilityAnswer::Reachable, true};
    }

    Query q(static_cast<NodeID>(source_comp), static_cast<NodeID>(target_comp));
    if (state_->config.pruning_dfs) {
        state_->index->singleQuery<FB_PDFS>(q);
    } else {
        state_->index->singleQuery<FB_PIBFS>(q);
    }

    const bool reachable = q.isPositive();
    const bool settled_by_observation = q.answeredWithObservation();

    return {
        reachable ? ReachabilityAnswer::Reachable : ReachabilityAnswer::Unreachable,
        settled_by_observation
    };
}

}  // namespace hbrick
