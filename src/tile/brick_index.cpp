#include "hbrick/tile/brick_index.hpp"

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/port_graph.hpp"
#include "hbrick/tile/tile_decomposition.hpp"

namespace hbrick {

BrickIndex BrickIndex::build(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSize nominal_tile_size,
    const uint64_t max_memory_bytes
) {
    BrickIndex index;
    index.status_ = BaselineStatus::NotRun;

    if (!nominal_tile_size.isValid()) {
        index.status_ = BaselineStatus::Failed;
        return index;
    }

    const TileDecomposition decomposition = TileDecomposition::decompose(
        graph.width(),
        graph.height(),
        nominal_tile_size
    );
    BrickTileIndex tile_index = BrickTileIndex::build(
        graph,
        layout,
        decomposition,
        max_memory_bytes
    );

    if (!tile_index.allTilesCompleted()) {
        BrickIndex failed_index;
        failed_index.status_ = BaselineStatus::SkippedByPolicy;
        failed_index.tile_index_ = std::move(tile_index);
        return failed_index;
    }

    return BrickIndex::assemble(std::move(tile_index), graph);
}

BrickIndex BrickIndex::assemble(
    BrickTileIndex tile_index,
    const DirectedGridGraph& graph
) {
    BrickIndex index;
    index.tile_index_ = std::move(tile_index);

    if (!index.tile_index_.allTilesCompleted()) {
        index.status_ = BaselineStatus::SkippedByPolicy;
        return index;
    }

    index.port_index_ = PortIndex::build(index.tile_index_, graph.numVertices());
    index.seam_edges_ = collectSeamEdges(
        graph.csrGraph(),
        index.tile_index_,
        index.port_index_
    );
    index.port_graph_ = buildPortGraphCsr(
        index.tile_index_,
        index.port_index_,
        index.seam_edges_
    );
    index.status_ = BaselineStatus::Completed;
    return index;
}

}  // namespace hbrick
