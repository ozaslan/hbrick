#include "hbrick/tile/base_tile_summary.hpp"

#include <chrono>
#include <exception>
#include <limits>

#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/scc_compressed_closure.hpp"
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

[[nodiscard]] bool hasInterTileTransition(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSlot& slot,
    const GridCoord coord
) noexcept {
    const uint32_t u = layout.vertexId(coord).value;

    const auto checkNeighbor = [&](const GridCoord nbr) noexcept -> bool {
        if (!layout.contains(nbr) || !layout.isPassable(nbr)) {
            return false;
        }
        const uint32_t v = layout.vertexId(nbr).value;
        // Check outgoing edge u -> v
        for (const uint32_t out_nbr : graph.outNeighbors(u)) {
            if (out_nbr == v) {
                return true;
            }
        }
        // Check incoming edge v -> u
        for (const uint32_t in_nbr : graph.outNeighbors(v)) {
            if (in_nbr == u) {
                return true;
            }
        }
        return false;
    };

    // West perimeter
    if (coord.x == slot.origin.x && coord.x > 0U) {
        if (checkNeighbor(GridCoord{coord.x - 1U, coord.y})) {
            return true;
        }
    }
    // East perimeter
    if (coord.x == slot.origin.x + slot.extent.width - 1U && coord.x + 1U < layout.width()) {
        if (checkNeighbor(GridCoord{coord.x + 1U, coord.y})) {
            return true;
        }
    }
    // North perimeter
    if (coord.y == slot.origin.y && coord.y > 0U) {
        if (checkNeighbor(GridCoord{coord.x, coord.y - 1U})) {
            return true;
        }
    }
    // South perimeter
    if (coord.y == slot.origin.y + slot.extent.height - 1U && coord.y + 1U < layout.height()) {
        if (checkNeighbor(GridCoord{coord.x, coord.y + 1U})) {
            return true;
        }
    }

    return false;
}

void collectPorts(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSlot& slot,
    const std::vector<GridCoord>& local_coords,
    std::vector<TilePort>& ports
) {
    ports.clear();

    const uint64_t cell_count =
        static_cast<uint64_t>(slot.extent.width) *
        static_cast<uint64_t>(slot.extent.height);
    if (cell_count > std::numeric_limits<std::size_t>::max()) {
        return;  // tile too large to index; defensive, not practically reachable
    }
    std::vector<TilePort> perimeter_ports;
    perimeter_ports.reserve(local_coords.size());
    std::vector<uint32_t> port_index_by_cell(
        static_cast<std::size_t>(cell_count),
        std::numeric_limits<uint32_t>::max()
    );

    for (uint32_t local_index = 0U; local_index < local_coords.size(); ++local_index) {
        const GridCoord& coord = local_coords[local_index];
        if (!isOnTilePerimeter(coord, slot)) {
            continue;
        }
        if (!hasInterTileTransition(graph, layout, slot, coord)) {
            continue;
        }

        const size_t cell_index =
            static_cast<size_t>(coord.y - slot.origin.y) * slot.extent.width
            + static_cast<size_t>(coord.x - slot.origin.x);

        TilePort port{};
        port.coord = coord;
        port.local_index = local_index;
        port.side = portSideForCoord(coord, slot);
        port_index_by_cell[cell_index] = static_cast<uint32_t>(perimeter_ports.size());
        perimeter_ports.push_back(port);
    }

    const std::vector<GridCoord> boundary_order = canonicalBoundaryCoords(slot);
    ports.reserve(perimeter_ports.size());

    for (const GridCoord boundary_coord : boundary_order) {
        if (!layout.isPassable(boundary_coord)) {
            continue;
        }

        const size_t cell_index =
            static_cast<size_t>(boundary_coord.y - slot.origin.y) * slot.extent.width
            + static_cast<size_t>(boundary_coord.x - slot.origin.x);
        const uint32_t port_index = port_index_by_cell[cell_index];
        if (port_index == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        ports.push_back(perimeter_ports[port_index]);
    }
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
        const BitVector& from_row = summary.local_closure.row(local_from);
        summary.boundary_to_vertex.row(port_from) = from_row;

        for (uint32_t port_to = 0U; port_to < num_ports; ++port_to) {
            const uint32_t local_to = summary.ports[port_to].local_index;
            if (from_row.test(local_to)) {
                summary.boundary_summary.set(port_from, port_to);
            }
        }
    }

    for (uint32_t local_from = 0U; local_from < num_local; ++local_from) {
        const BitVector& from_row = summary.local_closure.row(local_from);
        BitVector& v_to_b = summary.vertex_to_boundary.row(local_from);
        for (uint32_t port_to = 0U; port_to < num_ports; ++port_to) {
            const uint32_t local_to = summary.ports[port_to].local_index;
            if (from_row.test(local_to)) {
                v_to_b.set(port_to);
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
    BitMatrix* closure_scratch,
    const bool omit_local_closure
) {
    PreprocessMemoryLedger ledger(max_memory_bytes);
    return buildBaseTile(
        graph,
        layout,
        slot,
        ledger,
        closure_nanoseconds,
        closure_scratch,
        omit_local_closure
    );
}

BaseTileSummary buildBaseTile(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSlot& slot,
    PreprocessMemoryLedger& ledger,
    uint64_t* closure_nanoseconds,
    BitMatrix* closure_scratch,
    const bool omit_local_closure
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

    try {
        const uint64_t closure_started_ns = monotonicNowNanoseconds();
        const CsrGraph local_graph = buildInducedSubgraph(
            graph,
            layout,
            slot,
            summary.global_vertices
        );
        summary.local_closure = buildTileReflexiveAdjacencyOrThrow(
            local_graph,
            std::numeric_limits<uint64_t>::max()
        );
        const uint32_t squaring_count = (num_local <= 1U)
            ? 0U
            : static_cast<uint32_t>(std::bit_width(num_local - 1U));
        BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            summary.local_closure,
            squaring_count,
            closure_scratch
        );
        if (closure_nanoseconds != nullptr) {
            *closure_nanoseconds = monotonicNowNanoseconds() - closure_started_ns;
        }

        collectPorts(graph, layout, slot, summary.local_coords, summary.ports);
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

        if (omit_local_closure) {
            summary.omit_local_closure = true;
            summary.local_closure = BitMatrix{};
            ledger.releaseCharge(exactBitMatrixStorageBytes(num_local, num_local));
        }
    } catch (const std::bad_alloc&) {
        ledger.releaseCharge(ledger.chargedBytes() - charged_before);
        BaseTileSummary failed{};
        failed.slot = slot;
        failed.status = BaselineStatus::OutOfMemory;
        if (closure_nanoseconds != nullptr) {
            *closure_nanoseconds = 0U;
        }
        return failed;
    } catch (const std::exception&) {
        ledger.releaseCharge(ledger.chargedBytes() - charged_before);
        BaseTileSummary failed{};
        failed.slot = slot;
        failed.status = BaselineStatus::Failed;
        if (closure_nanoseconds != nullptr) {
            *closure_nanoseconds = 0U;
        }
        return failed;
    }

    summary.status = BaselineStatus::Completed;
    return summary;
}

}  // namespace hbrick
