#include "hbrick/baselines/brick_closure_baseline.hpp"

#include <exception>
#include <limits>

#include "hbrick/baselines/closure_matrix_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/port_index.hpp"

namespace hbrick {

namespace {

[[nodiscard]] ReachabilityAnswer queryWithPortClosure(
    const BrickIndex& index,
    const BitMatrix& port_closure,
    const uint32_t source,
    const uint32_t target
) noexcept {
    const BrickTileIndex& tiles = index.tiles();
    const PortIndex& ports = index.ports();

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

    if (source_tile == target_tile
        && source_summary.local_closure.test(source_local, target_local)) {
        return ReachabilityAnswer::Reachable;
    }

    const uint32_t num_ports = ports.numPorts();
    if (num_ports == 0U || port_closure.numRows() != num_ports
        || port_closure.numCols() != num_ports) {
        return ReachabilityAnswer::Unreachable;
    }

    for (uint32_t src_tile_port = 0U; src_tile_port < source_summary.numPorts();
         ++src_tile_port) {
        if (!source_summary.vertex_to_boundary.test(source_local, src_tile_port)) {
            continue;
        }

        const uint32_t src_port_id =
            ports.portIdForTilePort(source_tile, src_tile_port);
        if (src_port_id == std::numeric_limits<uint32_t>::max()
            || src_port_id >= num_ports) {
            continue;
        }

        for (uint32_t dst_tile_port = 0U; dst_tile_port < target_summary.numPorts();
             ++dst_tile_port) {
            if (!target_summary.boundary_to_vertex.test(dst_tile_port, target_local)) {
                continue;
            }

            const uint32_t dst_port_id =
                ports.portIdForTilePort(target_tile, dst_tile_port);
            if (dst_port_id == std::numeric_limits<uint32_t>::max()
                || dst_port_id >= num_ports) {
                continue;
            }

            if (port_closure.test(src_port_id, dst_port_id)) {
                return ReachabilityAnswer::Reachable;
            }
        }
    }

    return ReachabilityAnswer::Unreachable;
}

}  // namespace

void BrickClosureBaseline::preprocess(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSize nominal_tile_size,
    const uint64_t max_memory_bytes
) {
    status_ = BaselineStatus::NotRun;
    index_ = BrickIndex{};
    port_closure_ = BitMatrix{};
    port_closure_scratch_ = BitMatrix{};

    index_ = BrickIndex::build(graph, layout, nominal_tile_size, max_memory_bytes);
    if (index_.status() != BaselineStatus::Completed) {
        status_ = index_.status();
        return;
    }

    const uint32_t num_ports = index_.ports().numPorts();
    if (!ClosureMatrixBuilder::canAllocateReflexiveAdjacency(num_ports, max_memory_bytes)) {
        status_ = BaselineStatus::SkippedByPolicy;
        return;
    }

    try {
        port_closure_ = ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(
            index_.portGraph(),
            max_memory_bytes
        );
        ClosureMatrixBuilder::transitiveClosureKleeneTruncatedInPlace(
            port_closure_,
            index_.portGraph(),
            &port_closure_scratch_
        );
        status_ = BaselineStatus::Completed;
    } catch (const std::exception&) {
        status_ = BaselineStatus::Failed;
        port_closure_ = BitMatrix{};
        port_closure_scratch_ = BitMatrix{};
    }
}

ReachabilityAnswer BrickClosureBaseline::query(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source >= index_.tiles().decomposition().mapWidth()
            * index_.tiles().decomposition().mapHeight()
        || target >= index_.tiles().decomposition().mapWidth()
            * index_.tiles().decomposition().mapHeight()) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source == target) {
        return ReachabilityAnswer::Reachable;
    }

    return queryWithPortClosure(index_, port_closure_, source, target);
}

uint64_t BrickClosureBaseline::indexStorageBytes() const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return 0U;
    }
    return port_closure_.memoryBytes();
}

}  // namespace hbrick

