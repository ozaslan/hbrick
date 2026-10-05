#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/baselines/hbrick_hierarchy_query.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <span>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/tile_micro_bfs.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/port_index.hpp"
#include "hbrick/tile/region_node.hpp"
#include "hbrick/tile/super_tile_summary.hpp"

namespace hbrick {

namespace {

void propagateSourceExteriorStep(
    const HBrickIndex& index,
    const std::span<const RegionNodeId> source_chain,
    const uint32_t chain_index,
    HBrickQueryScratch& scratch
) noexcept {
    if (chain_index + 1U >= source_chain.size()) {
        return;
    }
    if (!scratch.sourceChain()[chain_index].any()) {
        return;
    }

    const HierarchyTree& hierarchy = index.hierarchy();
    const RegionNodeId child_id = source_chain[chain_index];
    const RegionNodeId parent_id = source_chain[chain_index + 1U];
    const RegionNode& child_node = hierarchy.node(child_id.level, child_id.index);
    const SuperTileSummary& parent_summary =
        index.superSummary(parent_id.level, parent_id.index);
    if (parent_summary.status != BaselineStatus::Completed) {
        return;
    }
    const uint32_t embedding_index = childEmbeddingIndex(child_node, parent_summary);
    if (embedding_index == std::numeric_limits<uint32_t>::max()) {
        return;
    }

    BitVector& parent_vector = scratch.sourceChain()[chain_index + 1U];

    // Level-0 children expose base-tile perimeter ports. Level ≥1 children
    // expose exterior boundary ports only (see childBoundaryFromSuperTile),
    // so the chain vector already matches the embedding column space.
    embedChildPorts(
        scratch.sourceChain()[chain_index],
        parent_summary.child_port_to_gamma[embedding_index],
        scratch.gammaA()
    );

    multiplyVectorClosure(
        scratch.gammaA(),
        parent_summary.interface_closure,
        scratch.gammaB()
    );
    projectToExterior(
        scratch.gammaB(),
        parent_summary.exterior_gamma_indices,
        parent_vector
    );
}

void propagateTargetExteriorStep(
    const HBrickIndex& index,
    const std::span<const RegionNodeId> target_chain,
    const uint32_t chain_index,
    HBrickQueryScratch& scratch
) noexcept {
    if (chain_index + 1U >= target_chain.size()) {
        return;
    }
    if (!scratch.targetChain()[chain_index].any()) {
        return;
    }

    const HierarchyTree& hierarchy = index.hierarchy();
    const RegionNodeId child_id = target_chain[chain_index];
    const RegionNodeId parent_id = target_chain[chain_index + 1U];
    const RegionNode& child_node = hierarchy.node(child_id.level, child_id.index);
    const SuperTileSummary& parent_summary =
        index.superSummary(parent_id.level, parent_id.index);
    if (parent_summary.status != BaselineStatus::Completed) {
        return;
    }
    const uint32_t embedding_index = childEmbeddingIndex(child_node, parent_summary);
    if (embedding_index == std::numeric_limits<uint32_t>::max()) {
        return;
    }

    BitVector& parent_vector = scratch.targetChain()[chain_index + 1U];

    embedChildPorts(
        scratch.targetChain()[chain_index],
        parent_summary.child_port_to_gamma[embedding_index],
        scratch.gammaA()
    );

    // Target labels are "ports that can reach t". multiplyVectorClosure
    // applies row-vector x·M; expanding those labels needs M = S̄^T so the
    // effective map is S̄·y in column-vector form.
    multiplyVectorClosure(
        scratch.gammaA(),
        parent_summary.interface_closure_transpose,
        scratch.gammaB()
    );
    projectToExterior(
        scratch.gammaB(),
        parent_summary.exterior_gamma_indices,
        parent_vector
    );
}

[[nodiscard]] ReachabilityAnswer queryFlatBrickPortBfsImpl(
    const BrickIndex& index,
    const DirectedGridGraph* graph,
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch,
    GraphSearchScratch& micro_bfs_scratch
) noexcept {
    const BrickTileIndex& tiles = index.tiles();
    const PortIndex& ports = index.ports();
    const CsrGraph& port_graph = index.portGraph();

    const uint32_t source_tile = tiles.tileIndexForGlobalVertex(source);
    const uint32_t target_tile = tiles.tileIndexForGlobalVertex(target);
    const uint32_t source_local = tiles.localIndexForGlobalVertex(source);
    const uint32_t target_local = tiles.localIndexForGlobalVertex(target);

    if (source_tile == std::numeric_limits<uint32_t>::max()
        || target_tile == std::numeric_limits<uint32_t>::max()
        || source_local == std::numeric_limits<uint32_t>::max()
        || target_local == std::numeric_limits<uint32_t>::max()) {
        return ReachabilityAnswer::Unreachable;
    }

    const BaseTileSummary& source_summary = tiles.summaryByIndex(source_tile);
    const BaseTileSummary& target_summary = tiles.summaryByIndex(target_tile);

    if (source_tile == target_tile) {
        if (!source_summary.omit_local_closure && source_summary.local_closure.numRows() > 0U) {
            if (source_summary.local_closure.test(source_local, target_local)) {
                return ReachabilityAnswer::Reachable;
            }
        } else if (graph != nullptr) {
            const TileSlot& slot = tiles.decomposition().slotByIndex(source_tile);
            if (TileMicroBfs::reachable(
                    *graph,
                    slot.origin,
                    slot.extent.width,
                    slot.extent.height,
                    source,
                    target,
                    micro_bfs_scratch
                ) == ReachabilityAnswer::Reachable) {
                return ReachabilityAnswer::Reachable;
            }
        }
    }

    const uint32_t num_port_vertices = ports.numPorts();
    if (num_port_vertices == 0U) {
        return ReachabilityAnswer::Unreachable;
    }

    assert(scratch.isSizedFor(num_port_vertices));
    const uint32_t mark = scratch.nextMark();
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<uint32_t>& queue = scratch.queue();
    queue.clear();

    for (uint32_t tile_port_index = 0U; tile_port_index < source_summary.numPorts();
         ++tile_port_index) {
        if (!source_summary.vertex_to_boundary.test(source_local, tile_port_index)) {
            continue;
        }

        const uint32_t port_id =
            ports.portIdForTilePort(source_tile, tile_port_index);
        if (port_id == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        if (visited[port_id] == mark) {
            continue;
        }

        if (port_id < num_port_vertices
            && target_tile == ports.port(port_id).tile_index
            && target_summary.boundary_to_vertex.test(
                ports.port(port_id).tile_port_index,
                target_local
            )) {
            return ReachabilityAnswer::Reachable;
        }

        visited[port_id] = mark;
        queue.push_back(port_id);
    }

    if (queue.empty()) {
        return ReachabilityAnswer::Unreachable;
    }

    std::size_t head = 0U;
    while (head < queue.size()) {
        const uint32_t port_id = queue[head];
        ++head;

        for (const uint32_t neighbor : port_graph.outNeighbors(port_id)) {
            if (neighbor >= num_port_vertices || visited[neighbor] == mark) {
                continue;
            }

            if (target_tile == ports.port(neighbor).tile_index
                && target_summary.boundary_to_vertex.test(
                    ports.port(neighbor).tile_port_index,
                    target_local
                )) {
                return ReachabilityAnswer::Reachable;
            }

            visited[neighbor] = mark;
            queue.push_back(neighbor);
        }
    }

    return ReachabilityAnswer::Unreachable;
}

}  // namespace

ReachabilityAnswer HBrickBaseline::queryFlatBrickPortBfs(
    const BrickIndex& index,
    const DirectedGridGraph* graph,
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch,
    GraphSearchScratch& micro_bfs_scratch
) noexcept {
    return queryFlatBrickPortBfsImpl(index, graph, source, target, scratch, micro_bfs_scratch);
}

HBrickQueryOutcome HBrickBaseline::queryHierarchical(
    const HBrickIndex& index,
    const DirectedGridGraph* graph,
    const uint32_t source,
    const uint32_t target,
    HBrickQueryScratch& scratch,
    GraphSearchScratch& micro_bfs_scratch
) noexcept {
    HBrickQueryOutcome outcome;
    outcome.resolved_level = std::numeric_limits<uint32_t>::max();

    const BrickTileIndex& tiles = index.brickIndex().tiles();
    const HierarchyTree& hierarchy = index.hierarchy();

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

    if (source_tile == target_tile) {
        if (!source_summary.omit_local_closure && source_summary.local_closure.numRows() > 0U) {
            if (source_summary.local_closure.test(source_local, target_local)) {
                outcome.answer = ReachabilityAnswer::Reachable;
                outcome.local_hit = true;
                outcome.resolved_level = 0U;
                return outcome;
            }
        } else if (graph != nullptr) {
            const TileSlot& slot = tiles.decomposition().slotByIndex(source_tile);
            if (TileMicroBfs::reachable(
                    *graph,
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

        while (source_propagated < level - 1U) {
            propagateSourceExteriorStep(index, source_chain, source_propagated, scratch);
            ++source_propagated;
        }
        while (target_propagated < level - 1U) {
            propagateTargetExteriorStep(index, target_chain, target_propagated, scratch);
            ++target_propagated;
        }

        ++outcome.ancestors_checked;

        if (!scratch.sourceChain()[level - 1U].any()
            || !scratch.targetChain()[level - 1U].any()) {
            continue;
        }

        const SuperTileSummary& parent_summary =
            index.superSummary(level, source_chain[level].index);
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

    return outcome;
}

void HBrickBaseline::adoptPrebuiltIndex(
    HBrickIndex index,
    const DirectedGridGraph* graph
) {
    status_ = BaselineStatus::NotRun;
    index_ = HBrickIndex{};
    graph_ = graph;
    scratch_ = HBrickQueryScratch{};
    port_bfs_scratch_ = GraphSearchScratch{};
    micro_bfs_scratch_ = GraphSearchScratch{};

    index_ = std::move(index);
    status_ = index_.status();
    if (status_ == BaselineStatus::Completed) {
        scratch_.prepare(index_);
        port_bfs_scratch_.resetForGraph(index_.brickIndex().ports().numPorts());
        if (graph_ != nullptr) {
            micro_bfs_scratch_.resetForGraph(graph_->numVertices());
        }
    }
}

void HBrickBaseline::preprocess(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const HBrickConfig config
) {
    status_ = BaselineStatus::NotRun;
    index_ = HBrickIndex{};
    graph_ = &graph;
    scratch_ = HBrickQueryScratch{};
    port_bfs_scratch_ = GraphSearchScratch{};
    micro_bfs_scratch_ = GraphSearchScratch{};

    index_ = HBrickIndex::build(graph, layout, config);
    status_ = index_.status();
    if (status_ == BaselineStatus::Completed) {
        scratch_.prepare(index_);
        port_bfs_scratch_.resetForGraph(index_.brickIndex().ports().numPorts());
        micro_bfs_scratch_.resetForGraph(graph.numVertices());
    }
}

uint64_t HBrickBaseline::indexStorageBytes() const noexcept {
    return index_.estimateStorageBytes();
}

uint64_t HBrickBaseline::measuredStorageBytes() const noexcept {
    return index_.measureStorageBytes();
}

ReachabilityAnswer HBrickBaseline::query(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    return query(source, target, scratch_, port_bfs_scratch_, micro_bfs_scratch_);
}

ReachabilityAnswer HBrickBaseline::query(
    const uint32_t source,
    const uint32_t target,
    HBrickQueryScratch& scratch,
    GraphSearchScratch& port_bfs_scratch
) const noexcept {
    return query(source, target, scratch, port_bfs_scratch, micro_bfs_scratch_);
}

ReachabilityAnswer HBrickBaseline::query(
    const uint32_t source,
    const uint32_t target,
    HBrickQueryScratch& scratch,
    GraphSearchScratch& port_bfs_scratch,
    GraphSearchScratch& micro_bfs_scratch
) const noexcept {
    return queryDetailed(source, target, scratch, port_bfs_scratch, micro_bfs_scratch).answer;
}

HBrickQueryOutcome HBrickBaseline::queryDetailed(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    return queryDetailed(source, target, scratch_, port_bfs_scratch_, micro_bfs_scratch_);
}

HBrickQueryOutcome HBrickBaseline::queryDetailed(
    const uint32_t source,
    const uint32_t target,
    HBrickQueryScratch& scratch,
    GraphSearchScratch& port_bfs_scratch
) const noexcept {
    return queryDetailed(source, target, scratch, port_bfs_scratch, micro_bfs_scratch_);
}

HBrickQueryOutcome HBrickBaseline::queryDetailed(
    const uint32_t source,
    const uint32_t target,
    HBrickQueryScratch& scratch,
    GraphSearchScratch& port_bfs_scratch,
    GraphSearchScratch& micro_bfs_scratch
) const noexcept {
    HBrickQueryOutcome outcome;
    outcome.resolved_level = std::numeric_limits<uint32_t>::max();
    if (status_ != BaselineStatus::Completed) {
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
        outcome.answer = queryFlatBrickPortBfs(
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

    outcome = queryHierarchical(index_, graph_, source, target, scratch, micro_bfs_scratch);
    if (outcome.answer == ReachabilityAnswer::Reachable) {
        return outcome;
    }

    if (index_.hierarchyQuerySound()) {
        return outcome;
    }

    outcome.answer = queryFlatBrickPortBfs(
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
