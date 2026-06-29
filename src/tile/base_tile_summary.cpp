#include "hbrick/tile/base_tile_summary.hpp"

#include <chrono>
#include <limits>

#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/connected_components.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/tile_boundary_order.hpp"
#include "hbrick/tile/tile_closure_util.hpp"
#include "hbrick/tile/tile_port.hpp"

namespace hbrick {

namespace {

[[nodiscard]] uint64_t monotonicNowNanoseconds() noexcept {
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock::now().time_since_epoch()
    ).count());
}

void collectLocalVertices(
    const MazeLayout& layout,
    const TileSlot& slot,
    std::vector<GridCoord>& local_coords,
    std::vector<uint32_t>& global_vertices
) {
    local_coords.clear();
    global_vertices.clear();

    for (uint32_t y = slot.origin.y; y < slot.maxY(); ++y) {
        for (uint32_t x = slot.origin.x; x < slot.maxX(); ++x) {
            const GridCoord coord{x, y};
            if (!layout.isPassable(coord)) {
                continue;
            }
            local_coords.push_back(coord);
            global_vertices.push_back(layout.vertexId(coord).value);
        }
    }
}

void collectPorts(
    const MazeLayout& layout,
    const TileSlot& slot,
    const std::vector<GridCoord>& local_coords,
    std::vector<TilePort>& ports
) {
    ports.clear();

    for (uint32_t local_index = 0U; local_index < local_coords.size(); ++local_index) {
        const GridCoord& coord = local_coords[local_index];
        if (!isOnTilePerimeter(coord, slot)) {
            continue;
        }
        TilePort port{};
        port.coord = coord;
        port.local_index = local_index;
        port.side = portSideForCoord(coord, slot);
        ports.push_back(port);
    }

    const std::vector<GridCoord> boundary_order = canonicalBoundaryCoords(slot);
    std::vector<TilePort> ordered_ports;
    ordered_ports.reserve(ports.size());

    for (const GridCoord boundary_coord : boundary_order) {
        if (!layout.isPassable(boundary_coord)) {
            continue;
        }
        for (const TilePort& port : ports) {
            if (port.coord.x == boundary_coord.x && port.coord.y == boundary_coord.y) {
                ordered_ports.push_back(port);
                break;
            }
        }
    }

    ports.swap(ordered_ports);
}

CsrGraph buildInducedSubgraph(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSlot& slot,
    const std::vector<uint32_t>& global_vertices
) {
    const uint32_t local_count = static_cast<uint32_t>(global_vertices.size());
    CsrGraphBuilder builder{local_count};

    std::vector<uint32_t> global_to_local(graph.numVertices(), std::numeric_limits<uint32_t>::max());
    for (uint32_t local_index = 0U; local_index < local_count; ++local_index) {
        global_to_local[global_vertices[local_index]] = local_index;
    }

    for (uint32_t local_from = 0U; local_from < local_count; ++local_from) {
        const uint32_t global_from = global_vertices[local_from];
        for (const uint32_t global_to : graph.outNeighbors(global_from)) {
            const uint32_t local_to = global_to_local[global_to];
            if (local_to == std::numeric_limits<uint32_t>::max()) {
                continue;
            }
            const GridCoord to_coord = graph.coordFromVertex(global_to);
            if (!slot.contains(to_coord) || !layout.isPassable(to_coord)) {
                continue;
            }
            builder.addEdge(local_from, local_to);
        }
    }

    return builder.build();
}

template<typename T>
[[nodiscard]] uint64_t vectorHeapBytes(const std::vector<T>& values) noexcept {
    return static_cast<uint64_t>(values.capacity()) * static_cast<uint64_t>(sizeof(T));
}

void projectBoundaryMatrices(BaseTileSummary& summary) {
    const uint32_t num_local = summary.numLocalVertices();
    const uint32_t num_ports = summary.numPorts();

    summary.boundary_summary = BitMatrix{num_ports, num_ports};
    summary.vertex_to_boundary = BitMatrix{num_local, num_ports};
    summary.boundary_to_vertex = BitMatrix{num_ports, num_local};

    for (uint32_t port_from = 0U; port_from < num_ports; ++port_from) {
        const uint32_t local_from = summary.ports[port_from].local_index;
        for (uint32_t port_to = 0U; port_to < num_ports; ++port_to) {
            const uint32_t local_to = summary.ports[port_to].local_index;
            if (summary.local_closure.test(local_from, local_to)) {
                summary.boundary_summary.set(port_from, port_to);
            }
        }
        for (uint32_t local_to = 0U; local_to < num_local; ++local_to) {
            if (summary.local_closure.test(local_from, local_to)) {
                summary.boundary_to_vertex.set(port_from, local_to);
            }
        }
    }

    for (uint32_t local_from = 0U; local_from < num_local; ++local_from) {
        for (uint32_t port_to = 0U; port_to < num_ports; ++port_to) {
            const uint32_t local_to = summary.ports[port_to].local_index;
            if (summary.local_closure.test(local_from, local_to)) {
                summary.vertex_to_boundary.set(local_from, port_to);
            }
        }
    }
}

}  // namespace

BaseTileSummary buildBaseTile(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSlot& slot,
    const uint64_t max_memory_bytes,
    uint64_t* closure_nanoseconds,
    BitMatrix* closure_scratch
) {
    PreprocessMemoryLedger ledger(max_memory_bytes);
    return buildBaseTile(
        graph,
        layout,
        slot,
        ledger,
        closure_nanoseconds,
        closure_scratch
    );
}

BaseTileSummary buildBaseTile(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSlot& slot,
    PreprocessMemoryLedger& ledger,
    uint64_t* closure_nanoseconds,
    BitMatrix* closure_scratch
) {
    BaseTileSummary summary{};
    summary.slot = slot;
    summary.status = BaselineStatus::NotRun;
    const uint64_t charged_before = ledger.chargedBytes();

    auto failPolicy = [&]() -> BaseTileSummary {
        ledger.releaseCharge(ledger.chargedBytes() - charged_before);
        BaseTileSummary skipped{};
        skipped.slot = slot;
        skipped.status = BaselineStatus::SkippedByPolicy;
        if (closure_nanoseconds != nullptr) {
            *closure_nanoseconds = 0U;
        }
        return skipped;
    };

    collectLocalVertices(layout, slot, summary.local_coords, summary.global_vertices);
    if (!ledger.tryCharge(
            vectorHeapBytes(summary.local_coords) + vectorHeapBytes(summary.global_vertices)
        )) {
        return failPolicy();
    }

    const uint32_t num_local = summary.numLocalVertices();
    if (num_local == 0U) {
        if (closure_nanoseconds != nullptr) {
            *closure_nanoseconds = 0U;
        }
        summary.status = BaselineStatus::Completed;
        return summary;
    }

    if (!ledger.tryCharge(exactBitMatrixStorageBytes(num_local, num_local))) {
        return failPolicy();
    }

    const uint64_t closure_started_ns = monotonicNowNanoseconds();
    const CsrGraph local_graph = buildInducedSubgraph(graph, layout, slot, summary.global_vertices);
    summary.local_closure = buildTileReflexiveAdjacencyOrThrow(
        local_graph,
        std::numeric_limits<uint64_t>::max()
    );
    const uint32_t largest_component_size =
        largestUndirectedComponentSize(local_graph);
    const uint32_t squaring_count =
        BooleanClosure::kleeneSquaringCountForLargestComponent(largest_component_size);
    BooleanClosure::transitiveClosureKleeneSquaringInPlace(
        summary.local_closure,
        squaring_count,
        closure_scratch
    );
    if (closure_nanoseconds != nullptr) {
        *closure_nanoseconds = monotonicNowNanoseconds() - closure_started_ns;
    }

    collectPorts(layout, slot, summary.local_coords, summary.ports);
    if (!ledger.tryCharge(vectorHeapBytes(summary.ports))) {
        return failPolicy();
    }

    const uint32_t num_ports = summary.numPorts();
    const uint64_t boundary_matrix_bytes =
        exactBitMatrixStorageBytes(num_ports, num_ports)
        + exactBitMatrixStorageBytes(num_local, num_ports)
        + exactBitMatrixStorageBytes(num_ports, num_local);
    if (!ledger.tryCharge(boundary_matrix_bytes)) {
        return failPolicy();
    }

    projectBoundaryMatrices(summary);
    summary.status = BaselineStatus::Completed;
    return summary;
}

}  // namespace hbrick
