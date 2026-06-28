#include "hbrick/tile/brick_index.hpp"

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/brick_index_builder.hpp"
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

}  // namespace hbrick
