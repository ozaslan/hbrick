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
    index.tile_index_ = BrickTileIndex::build(
        graph,
        layout,
        decomposition,
        max_memory_bytes
    );

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
