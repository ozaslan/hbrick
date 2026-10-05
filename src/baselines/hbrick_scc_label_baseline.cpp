/**
 * @file hbrick_scc_label_baseline.cpp
 * @brief Implementation of H-BRICK baseline with SCC-condensed materialized ancestor labels.
 */

#include "hbrick/baselines/hbrick_scc_label_baseline.hpp"
#include "hbrick/baselines/hbrick_hierarchy_query.hpp"

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/scc_decomposition.hpp"
#include "hbrick/graph/tile_micro_bfs.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/region_node.hpp"
#include "hbrick/tile/super_tile_summary.hpp"

namespace hbrick {

void HBrickSccLabelBaseline::preprocess(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    HBrickConfig config
) {
    adoptPrebuiltIndex(HBrickIndex::build(graph, layout, config), &graph);
}

void HBrickSccLabelBaseline::adoptPrebuiltIndex(
    HBrickIndex index,
    const DirectedGridGraph* graph
) {
    index_ = std::move(index);
    graph_ = graph;
    status_ = index_.status();
    tile_labels_.clear();

    if (status_ == BaselineStatus::Completed) {
        materializeSccLabels();
        if (indexStorageBytes() > index_.config().max_memory_bytes) {
            tile_labels_.clear();
            tile_labels_.shrink_to_fit();
            status_ = BaselineStatus::OutOfMemory;
            return;
        }
        port_bfs_scratch_.resetForGraph(index_.brickIndex().ports().numPorts());
        if (graph_ != nullptr) {
            micro_bfs_scratch_.resetForGraph(graph_->numVertices());
        }
    }
}

void HBrickSccLabelBaseline::materializeSccLabels() {
    const BrickTileIndex& tiles = index_.brickIndex().tiles();
    const HierarchyTree& hierarchy = index_.hierarchy();
    const uint32_t num_tiles = tiles.decomposition().numSlots();
    const uint32_t num_levels = hierarchy.numLevels();

    tile_labels_.clear();
    tile_labels_.resize(num_tiles);

    GraphSearchScratch scratch{};

    for (uint32_t tile_idx = 0U; tile_idx < num_tiles; ++tile_idx) {
        const BaseTileSummary& summary = tiles.summaryByIndex(tile_idx);
        const TileSlot& slot = tiles.decomposition().slotByIndex(tile_idx);
        const uint32_t num_local = summary.numLocalVertices();
        TileSccLabels& tile_entry = tile_labels_[tile_idx];
        TileSccPartition& part = tile_entry.partition;
        part.cell_to_scc.assign(num_local, std::numeric_limits<uint32_t>::max());

        if (num_local == 0U) {
            part.num_sccs = 0U;
            continue;
        }

        // Partition local cells into SCCs.
        if (!summary.omit_local_closure && summary.local_closure.numRows() == num_local) {
            uint32_t next_scc = 0U;
            for (uint32_t i = 0U; i < num_local; ++i) {
                if (part.cell_to_scc[i] != std::numeric_limits<uint32_t>::max()) {
                    continue;
                }
                const uint32_t current_scc = next_scc++;
                part.cell_to_scc[i] = current_scc;
                for (uint32_t j = i + 1U; j < num_local; ++j) {
                    if (part.cell_to_scc[j] == std::numeric_limits<uint32_t>::max()
                        && summary.local_closure.test(i, j)
                        && summary.local_closure.test(j, i)) {
                        part.cell_to_scc[j] = current_scc;
                    }
                }
            }
            part.num_sccs = next_scc;
        } else if (graph_ != nullptr) {
            // Build local intra-tile CSR graph and run Tarjan / Kosaraju SCC.
            std::vector<uint32_t> cell_lookup(
                static_cast<size_t>(slot.extent.width) * static_cast<size_t>(slot.extent.height),
                std::numeric_limits<uint32_t>::max()
            );
            for (uint32_t i = 0U; i < num_local; ++i) {
                const GridCoord& c = summary.local_coords[i];
                const size_t cell_k =
                    static_cast<size_t>(c.y - slot.origin.y) * slot.extent.width
                    + static_cast<size_t>(c.x - slot.origin.x);
                cell_lookup[cell_k] = i;
            }

            CsrGraphBuilder builder(num_local);

            for (uint32_t i = 0U; i < num_local; ++i) {
                const uint32_t g_u = summary.global_vertices[i];
                for (const uint32_t g_v : graph_->outNeighbors(g_u)) {
                    const GridCoord c_v = graph_->coordFromVertex(g_v);
                    if (c_v.x >= slot.origin.x && c_v.x < slot.origin.x + slot.extent.width
                        && c_v.y >= slot.origin.y && c_v.y < slot.origin.y + slot.extent.height) {
                        const size_t cell_k =
                            static_cast<size_t>(c_v.y - slot.origin.y) * slot.extent.width
                            + static_cast<size_t>(c_v.x - slot.origin.x);
                        const uint32_t v_local = cell_lookup[cell_k];
                        if (v_local != std::numeric_limits<uint32_t>::max()) {
                            builder.addEdge(i, v_local);
                        }
                    }
                }
            }

            CsrGraph local_csr = builder.build();
            const SccDecomposition scc = SccDecomposition::compute(local_csr, scratch);
            part.num_sccs = scc.numComponents();
            for (uint32_t i = 0U; i < num_local; ++i) {
                part.cell_to_scc[i] = scc.componentOf(i);
            }
        } else {
            // Fallback: trivial 1-to-1 partition.
            part.num_sccs = num_local;
            for (uint32_t i = 0U; i < num_local; ++i) {
                part.cell_to_scc[i] = i;
            }
        }

        // Precompute cumulative ancestor labels per local SCC.
        const uint32_t num_sccs = part.num_sccs;
        tile_entry.labels.resize(num_sccs);
        const std::span<const RegionNodeId> chain = hierarchy.ancestorChain(tile_idx);

        // Find a representative local vertex for each SCC.
        std::vector<uint32_t> scc_representative(num_sccs, std::numeric_limits<uint32_t>::max());
        for (uint32_t i = 0U; i < num_local; ++i) {
            const uint32_t scc_id = part.cell_to_scc[i];
            if (scc_representative[scc_id] == std::numeric_limits<uint32_t>::max()) {
                scc_representative[scc_id] = i;
            }
        }

        const uint32_t num_ports = summary.numPorts();

        for (uint32_t scc_id = 0U; scc_id < num_sccs; ++scc_id) {
            tile_entry.labels[scc_id].resize(num_levels);
            const uint32_t rep = scc_representative[scc_id];
            if (rep == std::numeric_limits<uint32_t>::max()) {
                continue;
            }

            // Base perimeter ports reachable from this SCC.
            BitVector current_source(num_ports);
            if (rep < summary.vertex_to_boundary.numRows()) {
                current_source.rowOrFrom(summary.vertex_to_boundary.row(rep));
            }

            // Base perimeter ports that can reach into this SCC.
            BitVector current_target(num_ports);
            const size_t rep_word = rep / 64U;
            const uint64_t rep_mask = 1ULL << (rep % 64U);
            for (uint32_t p = 0U; p < num_ports; ++p) {
                if ((summary.boundary_to_vertex.row(p).word(rep_word) & rep_mask) != 0U) {
                    current_target.set(p);
                }
            }

            // Lift through ancestor chain levels 1 .. num_levels - 1.
            for (uint32_t level = 1U; level < chain.size(); ++level) {
                const RegionNodeId child_id = chain[level - 1U];
                const RegionNodeId parent_id = chain[level];
                const RegionNode& child_node = hierarchy.node(child_id.level, child_id.index);
                const SuperTileSummary& parent_summary =
                    index_.superSummary(parent_id.level, parent_id.index);

                if (parent_summary.status != BaselineStatus::Completed) {
                    break;
                }

                const uint32_t embedding_index = childEmbeddingIndex(child_node, parent_summary);
                if (embedding_index == std::numeric_limits<uint32_t>::max()) {
                    break;
                }

                const uint32_t gamma_size = parent_summary.interface_closure.numRows();
                BitVector gamma_A(gamma_size);
                embedChildPorts(
                    current_source,
                    parent_summary.child_port_to_gamma[embedding_index],
                    gamma_A
                );

                BitVector gamma_F(gamma_size);
                multiplyVectorClosure(
                    gamma_A,
                    parent_summary.interface_closure,
                    gamma_F
                );

                tile_entry.labels[scc_id][level].forward_gamma = gamma_F;

                BitVector next_source(parent_summary.exterior_gamma_indices.size());
                projectToExterior(
                    gamma_F,
                    parent_summary.exterior_gamma_indices,
                    next_source
                );
                current_source = std::move(next_source);

                BitVector gamma_B(gamma_size);
                embedChildPorts(
                    current_target,
                    parent_summary.child_port_to_gamma[embedding_index],
                    gamma_B
                );

                tile_entry.labels[scc_id][level].reverse_gamma = gamma_B;

                BitVector gamma_R_closure(gamma_size);
                multiplyVectorClosure(
                    gamma_B,
                    parent_summary.interface_closure_transpose,
                    gamma_R_closure
                );

                BitVector next_target(parent_summary.exterior_gamma_indices.size());
                projectToExterior(
                    gamma_R_closure,
                    parent_summary.exterior_gamma_indices,
                    next_target
                );
                current_target = std::move(next_target);
            }
        }
    }

    port_bfs_scratch_.resetForGraph(index_.brickIndex().ports().numPorts());
    if (graph_ != nullptr) {
        micro_bfs_scratch_.resetForGraph(graph_->numVertices());
    }
}

ReachabilityAnswer HBrickSccLabelBaseline::query(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    if (status() != BaselineStatus::Completed) {
        return ReachabilityAnswer::Unreachable;
    }

    const uint32_t num_vertices =
        index_.brickIndex().tiles().decomposition().mapWidth()
        * index_.brickIndex().tiles().decomposition().mapHeight();
    if (source >= num_vertices || target >= num_vertices) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source == target) {
        return ReachabilityAnswer::Reachable;
    }

    if (!index_.hasSuperLevel(1U)) {
        return HBrickBaseline::queryFlatBrickPortBfs(
            index_.brickIndex(),
            graph_,
            source,
            target,
            port_bfs_scratch_,
            micro_bfs_scratch_
        );
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
        return ReachabilityAnswer::Unreachable;
    }

    const BaseTileSummary& source_summary = tiles.summaryByIndex(source_tile);

    // Intra-tile check.
    if (source_tile == target_tile) {
        if (!source_summary.omit_local_closure && source_summary.local_closure.numRows() > 0U) {
            if (source_summary.local_closure.test(source_local, target_local)) {
                return ReachabilityAnswer::Reachable;
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
                return ReachabilityAnswer::Reachable;
            }
        }
    }

    const TileSccLabels& source_labels = tile_labels_[source_tile];
    const TileSccLabels& target_labels = tile_labels_[target_tile];
    if (source_local >= source_labels.partition.cell_to_scc.size()
        || target_local >= target_labels.partition.cell_to_scc.size()) {
        return ReachabilityAnswer::Unreachable;
    }

    const uint32_t source_scc = source_labels.partition.cell_to_scc[source_local];
    const uint32_t target_scc = target_labels.partition.cell_to_scc[target_local];
    if (source_scc >= source_labels.labels.size()
        || target_scc >= target_labels.labels.size()) {
        return ReachabilityAnswer::Unreachable;
    }

    const std::span<const RegionNodeId> source_chain = hierarchy.ancestorChain(source_tile);
    const std::span<const RegionNodeId> target_chain = hierarchy.ancestorChain(target_tile);
    const uint32_t shared_levels =
        std::min(static_cast<uint32_t>(source_chain.size()),
                 static_cast<uint32_t>(target_chain.size()));

    const auto& s_labels = source_labels.labels[source_scc];
    const auto& t_labels = target_labels.labels[target_scc];

    for (uint32_t level = 1U; level < shared_levels; ++level) {
        if (source_chain[level] != target_chain[level]) {
            continue;
        }
        if (level >= s_labels.size() || level >= t_labels.size()) {
            continue;
        }

        const BitVector& f_gamma = s_labels[level].forward_gamma;
        const BitVector& r_gamma = t_labels[level].reverse_gamma;
        if (sparseLabelIntersect(f_gamma, r_gamma)) {
            return ReachabilityAnswer::Reachable;
        }
    }

    if (index_.hierarchyQuerySound()) {
        return ReachabilityAnswer::Unreachable;
    }

    return HBrickBaseline::queryFlatBrickPortBfs(
        index_.brickIndex(),
        graph_,
        source,
        target,
        port_bfs_scratch_,
        micro_bfs_scratch_
    );
}

HBrickQueryOutcome HBrickSccLabelBaseline::queryDetailed(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    HBrickQueryOutcome outcome;
    outcome.resolved_level = std::numeric_limits<uint32_t>::max();

    if (status() != BaselineStatus::Completed) {
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

    const TileSccLabels& source_labels = tile_labels_[source_tile];
    const TileSccLabels& target_labels = tile_labels_[target_tile];
    if (source_local >= source_labels.partition.cell_to_scc.size()
        || target_local >= target_labels.partition.cell_to_scc.size()) {
        return outcome;
    }

    const uint32_t source_scc = source_labels.partition.cell_to_scc[source_local];
    const uint32_t target_scc = target_labels.partition.cell_to_scc[target_local];
    if (source_scc >= source_labels.labels.size()
        || target_scc >= target_labels.labels.size()) {
        return outcome;
    }

    const std::span<const RegionNodeId> source_chain = hierarchy.ancestorChain(source_tile);
    const std::span<const RegionNodeId> target_chain = hierarchy.ancestorChain(target_tile);
    const uint32_t shared_levels =
        std::min(static_cast<uint32_t>(source_chain.size()),
                 static_cast<uint32_t>(target_chain.size()));

    const auto& s_labels = source_labels.labels[source_scc];
    const auto& t_labels = target_labels.labels[target_scc];

    for (uint32_t level = 1U; level < shared_levels; ++level) {
        if (source_chain[level] != target_chain[level]) {
            continue;
        }
        ++outcome.ancestors_checked;
        if (level >= s_labels.size() || level >= t_labels.size()) {
            continue;
        }

        const BitVector& f_gamma = s_labels[level].forward_gamma;
        const BitVector& r_gamma = t_labels[level].reverse_gamma;
        if (sparseLabelIntersect(f_gamma, r_gamma)) {
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

uint64_t HBrickSccLabelBaseline::sccLabelsMemoryBytes() const noexcept {
    uint64_t bytes = static_cast<uint64_t>(tile_labels_.capacity()) * sizeof(TileSccLabels);

    for (const TileSccLabels& tl : tile_labels_) {
        bytes += static_cast<uint64_t>(tl.partition.cell_to_scc.capacity()) * sizeof(uint32_t);
        bytes += static_cast<uint64_t>(tl.labels.capacity()) * sizeof(std::vector<SccAncestorLabel>);
        for (const auto& scc_vec : tl.labels) {
            bytes += static_cast<uint64_t>(scc_vec.capacity()) * sizeof(SccAncestorLabel);
            for (const SccAncestorLabel& label : scc_vec) {
                bytes += static_cast<uint64_t>(label.forward_gamma.wordsCapacity()) * sizeof(uint64_t);
                bytes += static_cast<uint64_t>(label.reverse_gamma.wordsCapacity()) * sizeof(uint64_t);
            }
        }
    }

    return bytes;
}

uint64_t HBrickSccLabelBaseline::scratchMemoryBytes() const noexcept {
    return static_cast<uint64_t>(port_bfs_scratch_.memoryBytes())
        + static_cast<uint64_t>(micro_bfs_scratch_.memoryBytes());
}

uint64_t HBrickSccLabelBaseline::indexStorageBytes() const noexcept {
    return index_.measureStorageBytes() + sccLabelsMemoryBytes();
}

}  // namespace hbrick
