#include "hbrick/tile/brick_tile_index.hpp"

#include <limits>

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/preprocess_memory_ledger.hpp"

namespace hbrick {

BrickTileIndex BrickTileIndex::build(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileDecomposition& decomposition,
    const uint64_t max_memory_bytes
) {
    BrickTileIndex index;
    index.decomposition_ = decomposition;
    PreprocessMemoryLedger ledger(max_memory_bytes);
    const uint64_t summaries_bytes =
        static_cast<uint64_t>(decomposition.numSlots()) * sizeof(BaseTileSummary);
    if (!ledger.tryCharge(summaries_bytes)) {
        return index;
    }
    index.summaries_.resize(decomposition.numSlots());

    const uint64_t lookup_bytes =
        static_cast<uint64_t>(graph.numVertices()) * sizeof(uint32_t) * 2U;
    if (!ledger.tryCharge(lookup_bytes)) {
        ledger.releaseCharge(summaries_bytes);
        return index;
    }

    index.global_vertex_tile_index_.assign(graph.numVertices(), std::numeric_limits<uint32_t>::max());
    index.global_vertex_local_index_.assign(graph.numVertices(), std::numeric_limits<uint32_t>::max());

    for (uint32_t tile_j = 0U; tile_j < decomposition.numSlotsJ(); ++tile_j) {
        for (uint32_t tile_i = 0U; tile_i < decomposition.numSlotsI(); ++tile_i) {
            const uint32_t slot_index = tile_j * decomposition.numSlotsI() + tile_i;
            const TileSlot& slot = decomposition.slot(tile_i, tile_j);
            BaseTileSummary summary = buildBaseTile(graph, layout, slot, ledger);
            index.summaries_[slot_index] = std::move(summary);

            const BaseTileSummary& stored = index.summaries_[slot_index];
            if (stored.status != BaselineStatus::Completed) {
                continue;
            }

            for (uint32_t local_index = 0U; local_index < stored.numLocalVertices(); ++local_index) {
                const uint32_t global_vertex = stored.global_vertices[local_index];
                index.global_vertex_tile_index_[global_vertex] = slot_index;
                index.global_vertex_local_index_[global_vertex] = local_index;
            }
        }
    }

    return index;
}

const BaseTileSummary& BrickTileIndex::summary(
    const uint32_t tile_i,
    const uint32_t tile_j
) const noexcept {
    const uint32_t index = tile_j * decomposition_.numSlotsI() + tile_i;
    return summaries_[index];
}

const BaseTileSummary& BrickTileIndex::summaryByIndex(const uint32_t index) const noexcept {
    return summaries_[index];
}

uint32_t BrickTileIndex::tileIndexForGlobalVertex(const uint32_t global_vertex) const noexcept {
    if (global_vertex >= global_vertex_tile_index_.size()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return global_vertex_tile_index_[global_vertex];
}

uint32_t BrickTileIndex::localIndexForGlobalVertex(const uint32_t global_vertex) const noexcept {
    if (global_vertex >= global_vertex_local_index_.size()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return global_vertex_local_index_[global_vertex];
}

bool BrickTileIndex::allTilesCompleted() const noexcept {
    for (const BaseTileSummary& summary : summaries_) {
        if (summary.status != BaselineStatus::Completed) {
            return false;
        }
    }
    return true;
}

}  // namespace hbrick
