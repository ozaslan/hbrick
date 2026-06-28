#include "hbrick/tile/brick_index.hpp"

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_index_builder.hpp"
#include "hbrick/tile/seam_edge.hpp"
#include "hbrick/tile/tile_decomposition.hpp"

namespace hbrick {

BrickIndex BrickIndex::build(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSize nominal_tile_size,
    const uint64_t max_memory_bytes
) {
    if (!nominal_tile_size.isValid()) {
        BrickIndex failed_index;
        failed_index.status_ = BaselineStatus::Failed;
        return failed_index;
    }

    BrickIndexBuilder builder;
    builder.begin(graph, layout, nominal_tile_size, max_memory_bytes);
    while (builder.running() && !builder.step()) {
    }

    if (!builder.report().valid) {
        BrickIndex failed_index;
        failed_index.status_ = BaselineStatus::Failed;
        return failed_index;
    }

    if (builder.report().status != BaselineStatus::Completed) {
        BrickIndex failed_index = builder.takeIndex();
        if (failed_index.status_ == BaselineStatus::NotRun) {
            failed_index.status_ = builder.report().status;
        }
        return failed_index;
    }

    return builder.takeIndex();
}

uint64_t BrickIndex::estimateStorageBytes() const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return 0U;
    }

    uint64_t bytes = 0U;
    for (const BaseTileSummary& summary : tile_index_.summaries()) {
        bytes += summary.local_closure.memoryBytes();
        bytes += summary.boundary_summary.memoryBytes();
        bytes += summary.vertex_to_boundary.memoryBytes();
        bytes += summary.boundary_to_vertex.memoryBytes();
    }

    bytes += port_index_.estimateStorageBytes();
    bytes += port_graph_.estimateStorageBytes();
    bytes += static_cast<uint64_t>(seam_edges_.size()) * sizeof(SeamEdge);
    return bytes;
}

}  // namespace hbrick
