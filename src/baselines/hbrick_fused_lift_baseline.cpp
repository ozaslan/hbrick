/**
 * @file hbrick_fused_lift_baseline.cpp
 * @brief Implementation of H-BRICK baseline with precomputed rectangular lift operators.
 */

#include "hbrick/baselines/hbrick_fused_lift_baseline.hpp"
#include "hbrick/baselines/hbrick_hierarchy_query.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/tile_micro_bfs.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/region_node.hpp"
#include "hbrick/tile/super_tile_summary.hpp"

namespace hbrick {

void HBrickFusedLiftBaseline::preprocess(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    HBrickConfig config
) {
    adoptPrebuiltIndex(HBrickIndex::build(graph, layout, config), &graph);
}

void HBrickFusedLiftBaseline::adoptPrebuiltIndex(
    HBrickIndex index,
    const DirectedGridGraph* graph
) {
    index_ = std::move(index);
    graph_ = graph;
    status_ = index_.status();

    fused_lifts_ = FusedTreeLifts{};
    scratch_ = HBrickQueryScratch{};
    port_bfs_scratch_ = GraphSearchScratch{};
    micro_bfs_scratch_ = GraphSearchScratch{};

    if (status_ == BaselineStatus::Completed) {
        fused_lifts_ = FusedTreeLifts::build(index_);
        if (!fused_lifts_.isValid()) {
            status_ = BaselineStatus::Failed;
            return;
        }

        if (indexStorageBytes() > index_.config().max_memory_bytes) {
            fused_lifts_ = FusedTreeLifts{};
            status_ = BaselineStatus::OutOfMemory;
            return;
        }

        scratch_.prepare(index_);
        port_bfs_scratch_.resetForGraph(index_.brickIndex().ports().numPorts());
        if (graph_ != nullptr) {
            micro_bfs_scratch_.resetForGraph(graph_->numVertices());
        }
    }
}

uint64_t HBrickFusedLiftBaseline::scratchMemoryBytes() const noexcept {
    return scratch_.memoryBytes()
        + static_cast<uint64_t>(port_bfs_scratch_.memoryBytes())
        + static_cast<uint64_t>(micro_bfs_scratch_.memoryBytes());
}

uint64_t HBrickFusedLiftBaseline::indexStorageBytes() const noexcept {
    uint64_t total = index_.measureStorageBytes();
    total += fused_lifts_.memoryBytes();
    return total;
}

ReachabilityAnswer HBrickFusedLiftBaseline::query(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    return query(source, target, scratch_, port_bfs_scratch_, micro_bfs_scratch_);
}

HBrickQueryOutcome HBrickFusedLiftBaseline::queryDetailed(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    return queryDetailed(source, target, scratch_, port_bfs_scratch_, micro_bfs_scratch_);
}

ReachabilityAnswer HBrickFusedLiftBaseline::query(
    const uint32_t source,
    const uint32_t target,
    HBrickQueryScratch& scratch,
    GraphSearchScratch& port_bfs_scratch,
    GraphSearchScratch& micro_bfs_scratch
) const noexcept {
    return queryDetailed(source, target, scratch, port_bfs_scratch, micro_bfs_scratch).answer;
}

HBrickQueryOutcome HBrickFusedLiftBaseline::queryDetailed(
    const uint32_t source,
    const uint32_t target,
    HBrickQueryScratch& scratch,
    GraphSearchScratch& port_bfs_scratch,
    GraphSearchScratch& micro_bfs_scratch
) const noexcept {
    (void)port_bfs_scratch;
    HBrickQueryOutcome outcome;
    outcome.resolved_level = std::numeric_limits<uint32_t>::max();

    if (status_ != BaselineStatus::Completed || !fused_lifts_.isValid()) {
        return outcome;
    }

    const uint32_t num_vertices =
        index_.brickIndex().tiles().decomposition().mapWidth()
        * index_.brickIndex().tiles().decomposition().mapHeight();
    if (source >= num_vertices || target >= num_vertices) {
        return outcome;
    }

    if (source == target) {
        outcome.answer = ReachabilityAnswer::Reachable;
        outcome.local_hit = true;
        outcome.resolved_level = 0U;
        return outcome;
    }

    if (!index_.hasSuperLevel(1U)) {
        outcome.answer = HBrickBaseline::queryFlatBrickPortBfs(
            index_.brickIndex(),
            graph_,
            source,
            target,
            port_bfs_scratch,
            micro_bfs_scratch
        );
        return outcome;
    }

    const BrickTileIndex& tiles = index_.brickIndex().tiles();
    const HierarchyTree& hierarchy = index_.hierarchy();

    const uint32_t source_tile = tiles.tileIndexForGlobalVertex(source);
    const uint32_t target_tile = tiles.tileIndexForGlobalVertex(target);
    const uint32_t source_local = tiles.localIndexForGlobalVertex(source);
    const uint32_t target_local = tiles.localIndexForGlobalVertex(target);

    if (source_tile == std::numeric_limits<uint32_t>::max()
        || target_tile == std::numeric_limits<uint32_t>::max()
        || source_local == std::numeric_limits<uint32_t>::max()
        || target_local == std::numeric_limits<uint32_t>::max()) {
        return outcome;
    }

    const BaseTileSummary& source_summary = tiles.summaryByIndex(source_tile);
    const BaseTileSummary& target_summary = tiles.summaryByIndex(target_tile);

    // 1. Same-tile resolution
    if (source_tile == target_tile) {
        if (!source_summary.omit_local_closure && source_summary.local_closure.numRows() > 0U) {
            if (source_summary.local_closure.test(source_local, target_local)) {
                outcome.answer = ReachabilityAnswer::Reachable;
                outcome.local_hit = true;
                outcome.resolved_level = 0U;
                return outcome;
            }
        } else if (graph_ != nullptr) {
            const TileSlot& slot = tiles.decomposition().slotByIndex(source_tile);
            if (TileMicroBfs::reachable(
                    *graph_,
                    slot.origin,
                    slot.extent.width,
                    slot.extent.height,
                    source,
                    target,
                    micro_bfs_scratch
                ) == ReachabilityAnswer::Reachable) {
                outcome.answer = ReachabilityAnswer::Reachable;
                outcome.local_hit = true;
                outcome.resolved_level = 0U;
                return outcome;
            }
        }
    }

    scratch.clearLeafAndWorkspace();

    // 2. Base leaf boundary initialization
    BitVector& source_leaf = scratch.sourceChain()[0U];
    source_leaf.rowOrFrom(source_summary.vertex_to_boundary.row(source_local));
    if (!source_leaf.any()) {
        return outcome;
    }

    BitVector& target_leaf = scratch.targetChain()[0U];
    const size_t target_word_index = target_local / 64U;
    const uint64_t target_mask = 1ULL << (target_local % 64U);
    const uint32_t num_target_ports = target_summary.numPorts();
    for (uint32_t port_index = 0U; port_index < num_target_ports; ++port_index) {
        const uint64_t row_word =
            target_summary.boundary_to_vertex.row(port_index).word(target_word_index);
        if ((row_word & target_mask) != 0U) {
            target_leaf.set(port_index);
        }
    }
    if (!target_leaf.any()) {
        return outcome;
    }

    const std::span<const RegionNodeId> source_chain =
        hierarchy.ancestorChain(source_tile);
    const std::span<const RegionNodeId> target_chain =
        hierarchy.ancestorChain(target_tile);

    uint32_t highest_common_level = 0U;
    const uint32_t shared_levels =
        std::min(static_cast<uint32_t>(source_chain.size()),
                 static_cast<uint32_t>(target_chain.size()));
    for (uint32_t level = 1U; level < shared_levels; ++level) {
        if (source_chain[level] == target_chain[level]) {
            highest_common_level = level;
        }
    }

    if (highest_common_level >= 2U) {
        scratch.clearChainLevelsThrough(highest_common_level);
    }

    uint32_t source_propagated = 0U;
    uint32_t target_propagated = 0U;

    for (uint32_t level = 1U; level < shared_levels; ++level) {
        if (source_chain[level] != target_chain[level]) {
            continue;
        }

        // Fused lift propagation on source side
        while (source_propagated < level - 1U) {
            fusedLiftOneStep(
                index_,
                fused_lifts_,
                source_chain,
                source_propagated,
                true,
                scratch.sourceChain()[source_propagated],
                scratch.sourceChain()[source_propagated + 1U]
            );
            ++source_propagated;
        }

        while (target_propagated < level - 1U) {
            fusedLiftOneStep(
                index_,
                fused_lifts_,
                target_chain,
                target_propagated,
                false,
                scratch.targetChain()[target_propagated],
                scratch.targetChain()[target_propagated + 1U]
            );
            ++target_propagated;
        }

        ++outcome.ancestors_checked;

        if (!scratch.sourceChain()[level - 1U].any()
            || !scratch.targetChain()[level - 1U].any()) {
            continue;
        }

        const SuperTileSummary& parent_summary =
            index_.superSummary(level, source_chain[level].index);
        if (parent_summary.status != BaselineStatus::Completed) {
            continue;
        }
        const RegionNodeId child_source_id = source_chain[level - 1U];
        const RegionNodeId child_target_id = target_chain[level - 1U];
        const RegionNode& child_source =
            hierarchy.node(child_source_id.level, child_source_id.index);
        const RegionNode& child_target =
            hierarchy.node(child_target_id.level, child_target_id.index);

        const uint32_t child_source_index =
            childEmbeddingIndex(child_source, parent_summary);
        const uint32_t child_target_index =
            childEmbeddingIndex(child_target, parent_summary);
        if (child_source_index == std::numeric_limits<uint32_t>::max()
            || child_target_index == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        embedChildPorts(
            scratch.sourceChain()[level - 1U],
            parent_summary.child_port_to_gamma[child_source_index],
            scratch.gammaA()
        );
        embedChildPorts(
            scratch.targetChain()[level - 1U],
            parent_summary.child_port_to_gamma[child_target_index],
            scratch.gammaB()
        );

        if (bilinearMeet(
                scratch.gammaA(),
                parent_summary.interface_closure,
                scratch.gammaB(),
                parent_summary.interface_closure_transpose
            )) {
            outcome.answer = ReachabilityAnswer::Reachable;
            outcome.ancestor_hit = true;
            outcome.resolved_level = level;
            return outcome;
        }
    }

    if (index_.hierarchyQuerySound()) {
        return outcome;
    }

    outcome.answer = HBrickBaseline::queryFlatBrickPortBfs(
        index_.brickIndex(),
        graph_,
        source,
        target,
        port_bfs_scratch,
        micro_bfs_scratch
    );
    outcome.used_flat_fallback = true;
    return outcome;
}

}  // namespace hbrick
