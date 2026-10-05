/**
 * @file hbrick_fused_lift_cache_baseline.cpp
 * @brief Implementation of H-BRICK baseline with fused lifts and endpoint caching.
 */

#include "hbrick/baselines/hbrick_fused_lift_cache_baseline.hpp"
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

namespace {

void ensureLiftedThrough(
    const HBrickIndex& index,
    const FusedTreeLifts& fused_lifts,
    EndpointLiftCacheSlot& slot,
    const std::span<const RegionNodeId> chain,
    const uint32_t needed_index,
    const bool source_side
) noexcept {
    if (!slot.has_active_leaf) {
        return;
    }
    while (slot.lifted_through < needed_index) {
        if (slot.lifted_through + 1U >= slot.chain.size()
            || slot.lifted_through + 1U >= chain.size()) {
            break;
        }
        fusedLiftOneStep(
            index,
            fused_lifts,
            chain,
            slot.lifted_through,
            source_side,
            slot.chain[slot.lifted_through],
            slot.chain[slot.lifted_through + 1U]
        );
        ++slot.lifted_through;
    }
}

[[nodiscard]] uint32_t maxExteriorBitsAtLevel(const HBrickIndex& index, const uint32_t level) noexcept {
    if (level == 0U) {
        uint32_t max_bits = 0U;
        for (const BaseTileSummary& summary : index.brickIndex().tiles().summaries()) {
            max_bits = std::max(max_bits, summary.numPorts());
        }
        return max_bits;
    }
    uint32_t max_bits = 0U;
    for (const SuperTileSummary& summary : index.superLevel(level)) {
        max_bits = std::max(
            max_bits,
            static_cast<uint32_t>(summary.exterior_ports.size())
        );
    }
    return max_bits;
}

}  // namespace

void EndpointLiftCache::init(const uint32_t num_slots, const HBrickIndex& index) {
    slots_.clear();
    if (num_slots == 0U || index.status() != BaselineStatus::Completed) {
        return;
    }

    const HierarchyTree& hierarchy = index.hierarchy();
    const uint32_t num_levels = hierarchy.numLevels();

    slots_.resize(num_slots);
    for (auto& slot : slots_) {
        slot.vertex_id = UINT32_MAX;
        slot.tile_id = UINT32_MAX;
        slot.lifted_through = 0U;
        slot.has_active_leaf = false;
        slot.chain.resize(num_levels);
        for (uint32_t level = 0U; level < num_levels; ++level) {
            slot.chain[level] = BitVector(maxExteriorBitsAtLevel(index, level));
        }
    }
}

void EndpointLiftCache::clear() noexcept {
    for (auto& slot : slots_) {
        slot.vertex_id = UINT32_MAX;
        slot.tile_id = UINT32_MAX;
        slot.lifted_through = 0U;
        slot.has_active_leaf = false;
        for (auto& vec : slot.chain) {
            vec.clear();
        }
    }
}

uint64_t EndpointLiftCache::memoryBytes() const noexcept {
    uint64_t total = sizeof(EndpointLiftCache);
    total += static_cast<uint64_t>(slots_.capacity()) * sizeof(EndpointLiftCacheSlot);
    for (const auto& slot : slots_) {
        total += static_cast<uint64_t>(slot.chain.capacity()) * sizeof(BitVector);
        for (const auto& bv : slot.chain) {
            total += static_cast<uint64_t>(bv.wordsCapacity()) * sizeof(uint64_t);
        }
    }
    return total;
}

HBrickFusedLiftCacheBaseline::HBrickFusedLiftCacheBaseline(const uint32_t cache_slots)
    : cache_capacity_slots_(cache_slots) {}

void HBrickFusedLiftCacheBaseline::preprocess(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    HBrickConfig config
) {
    adoptPrebuiltIndex(HBrickIndex::build(graph, layout, config), &graph);
}

void HBrickFusedLiftCacheBaseline::adoptPrebuiltIndex(
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
    source_cache_ = EndpointLiftCache{};
    target_cache_ = EndpointLiftCache{};

    if (status_ == BaselineStatus::Completed) {
        fused_lifts_ = FusedTreeLifts::build(index_);
        if (!fused_lifts_.isValid()) {
            status_ = BaselineStatus::Failed;
            return;
        }

        scratch_.prepare(index_);
        source_cache_.init(cache_capacity_slots_, index_);
        target_cache_.init(cache_capacity_slots_, index_);

        if (indexStorageBytes() > index_.config().max_memory_bytes) {
            fused_lifts_ = FusedTreeLifts{};
            source_cache_ = EndpointLiftCache{};
            target_cache_ = EndpointLiftCache{};
            scratch_ = HBrickQueryScratch{};
            status_ = BaselineStatus::OutOfMemory;
            return;
        }

        port_bfs_scratch_.resetForGraph(index_.brickIndex().ports().numPorts());
        if (graph_ != nullptr) {
            micro_bfs_scratch_.resetForGraph(graph_->numVertices());
        }
    }
}

uint64_t HBrickFusedLiftCacheBaseline::endpointCacheMemoryBytes() const noexcept {
    return source_cache_.memoryBytes() + target_cache_.memoryBytes();
}

uint64_t HBrickFusedLiftCacheBaseline::scratchMemoryBytes() const noexcept {
    return scratch_.memoryBytes()
        + static_cast<uint64_t>(port_bfs_scratch_.memoryBytes())
        + static_cast<uint64_t>(micro_bfs_scratch_.memoryBytes());
}

uint64_t HBrickFusedLiftCacheBaseline::indexStorageBytes() const noexcept {
    return index_.measureStorageBytes()
        + fused_lifts_.memoryBytes()
        + endpointCacheMemoryBytes();
}

ReachabilityAnswer HBrickFusedLiftCacheBaseline::query(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    return queryDetailed(source, target).answer;
}

HBrickQueryOutcome HBrickFusedLiftCacheBaseline::queryDetailed(
    const uint32_t source,
    const uint32_t target
) const noexcept {
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
            port_bfs_scratch_,
            micro_bfs_scratch_
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

    // 1. Same tile check
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
                    micro_bfs_scratch_
                ) == ReachabilityAnswer::Reachable) {
                outcome.answer = ReachabilityAnswer::Reachable;
                outcome.local_hit = true;
                outcome.resolved_level = 0U;
                return outcome;
            }
        }
    }

    EndpointLiftCacheSlot& s_slot = source_cache_.slotFor(source);
    const std::span<const RegionNodeId> source_chain = hierarchy.ancestorChain(source_tile);

    if (s_slot.vertex_id != source) {
        s_slot.vertex_id = source;
        s_slot.tile_id = source_tile;
        s_slot.lifted_through = 0U;
        s_slot.chain[0U].clear();
        s_slot.chain[0U].rowOrFrom(source_summary.vertex_to_boundary.row(source_local));
        s_slot.has_active_leaf = s_slot.chain[0U].any();
    }

    if (!s_slot.has_active_leaf) {
        return outcome;
    }

    EndpointLiftCacheSlot& t_slot = target_cache_.slotFor(target);
    const std::span<const RegionNodeId> target_chain = hierarchy.ancestorChain(target_tile);

    if (t_slot.vertex_id != target) {
        t_slot.vertex_id = target;
        t_slot.tile_id = target_tile;
        t_slot.lifted_through = 0U;
        t_slot.chain[0U].clear();

        const size_t target_word_index = target_local / 64U;
        const uint64_t target_mask = 1ULL << (target_local % 64U);
        const uint32_t num_target_ports = target_summary.numPorts();
        for (uint32_t port_index = 0U; port_index < num_target_ports; ++port_index) {
            const uint64_t row_word =
                target_summary.boundary_to_vertex.row(port_index).word(target_word_index);
            if ((row_word & target_mask) != 0U) {
                t_slot.chain[0U].set(port_index);
            }
        }
        t_slot.has_active_leaf = t_slot.chain[0U].any();
    }

    if (!t_slot.has_active_leaf) {
        return outcome;
    }

    const uint32_t shared_levels =
        std::min(static_cast<uint32_t>(source_chain.size()),
                 static_cast<uint32_t>(target_chain.size()));

    for (uint32_t level = 1U; level < shared_levels; ++level) {
        if (source_chain[level] != target_chain[level]) {
            continue;
        }

        ensureLiftedThrough(
            index_,
            fused_lifts_,
            s_slot,
            source_chain,
            level - 1U,
            true
        );
        ensureLiftedThrough(
            index_,
            fused_lifts_,
            t_slot,
            target_chain,
            level - 1U,
            false
        );

        ++outcome.ancestors_checked;

        if (!s_slot.chain[level - 1U].any() || !t_slot.chain[level - 1U].any()) {
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
            s_slot.chain[level - 1U],
            parent_summary.child_port_to_gamma[child_source_index],
            scratch_.gammaA()
        );
        embedChildPorts(
            t_slot.chain[level - 1U],
            parent_summary.child_port_to_gamma[child_target_index],
            scratch_.gammaB()
        );

        if (bilinearMeet(
                scratch_.gammaA(),
                parent_summary.interface_closure,
                scratch_.gammaB(),
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
        port_bfs_scratch_,
        micro_bfs_scratch_
    );
    outcome.used_flat_fallback = true;
    return outcome;
}

}  // namespace hbrick
