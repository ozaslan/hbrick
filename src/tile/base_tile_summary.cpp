#include "hbrick/tile/base_tile_summary.hpp"

#include <limits>

#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/tile_boundary_order.hpp"
#include "hbrick/tile/tile_closure_util.hpp"
#include "hbrick/tile/tile_port.hpp"

namespace hbrick {

namespace {

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
    const uint64_t max_memory_bytes
) {
    BaseTileSummary summary{};
    summary.slot = slot;
    summary.status = BaselineStatus::NotRun;

    collectLocalVertices(layout, slot, summary.local_coords, summary.global_vertices);
    const uint32_t num_local = summary.numLocalVertices();
    if (num_local == 0U) {
        summary.status = BaselineStatus::Completed;
        return summary;
    }

    if (!canAllocateTileReflexiveAdjacency(num_local, max_memory_bytes)) {
        summary.status = BaselineStatus::SkippedByPolicy;
        return summary;
    }

    const CsrGraph local_graph = buildInducedSubgraph(graph, layout, slot, summary.global_vertices);
    summary.local_closure = buildTileReflexiveAdjacencyOrThrow(
        local_graph,
        max_memory_bytes
    );
    BooleanClosure::transitiveClosureWarshallInPlace(summary.local_closure);

    collectPorts(layout, slot, summary.local_coords, summary.ports);
    projectBoundaryMatrices(summary);
    summary.status = BaselineStatus::Completed;
    return summary;
}

}  // namespace hbrick
