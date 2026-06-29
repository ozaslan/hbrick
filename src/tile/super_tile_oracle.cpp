#include "hbrick/tile/super_tile_oracle.hpp"

#include <algorithm>
#include <limits>

#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/port_index.hpp"
#include "hbrick/tile/seam_edge.hpp"
#include "hbrick/tile/super_tile_composer.hpp"
#include "hbrick/tile/tile_boundary_order.hpp"
#include "hbrick/tile/tile_closure_util.hpp"
#include "hbrick/tile/tile_port.hpp"

namespace hbrick {

namespace {

[[nodiscard]] bool coordsEqual(const GridCoord lhs, const GridCoord rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] bool containsCoord(
    const std::span<const GridCoord> coords,
    const GridCoord target
) noexcept {
    for (const GridCoord coord : coords) {
        if (coordsEqual(coord, target)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] uint32_t gammaIndexForCoord(
    const GammaOrdering& gamma,
    const GridCoord coord
) noexcept {
    for (uint32_t index = 0U; index < gamma.ports.size(); ++index) {
        if (coordsEqual(gamma.ports[index], coord)) {
            return index;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

[[nodiscard]] uint32_t cellIndexForCoord(
    const RegionCellClosure& region_closure,
    const GridCoord coord
) noexcept {
    for (uint32_t index = 0U; index < region_closure.cell_coords.size(); ++index) {
        if (coordsEqual(region_closure.cell_coords[index], coord)) {
            return index;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

[[nodiscard]] bool compareGammaPortOrder(
    const TileSlot& parent_bbox,
    const GridCoord lhs,
    const GridCoord rhs
) noexcept {
    const bool lhs_exterior = isOnTilePerimeter(lhs, parent_bbox);
    const bool rhs_exterior = isOnTilePerimeter(rhs, parent_bbox);
    if (lhs_exterior != rhs_exterior) {
        return lhs_exterior;
    }

    if (lhs_exterior) {
        const std::vector<GridCoord> canonical = canonicalBoundaryCoords(parent_bbox);
        uint32_t lhs_rank = std::numeric_limits<uint32_t>::max();
        uint32_t rhs_rank = std::numeric_limits<uint32_t>::max();
        for (uint32_t index = 0U; index < canonical.size(); ++index) {
            if (coordsEqual(canonical[index], lhs)) {
                lhs_rank = index;
            }
            if (coordsEqual(canonical[index], rhs)) {
                rhs_rank = index;
            }
        }
        if (lhs_rank != rhs_rank) {
            return lhs_rank < rhs_rank;
        }
    }

    if (lhs.y != rhs.y) {
        return lhs.y < rhs.y;
    }
    return lhs.x < rhs.x;
}

void collectRegionCells(
    const MazeLayout& layout,
    const TileSlot& region_bbox,
    std::vector<GridCoord>& cell_coords,
    std::vector<uint32_t>& global_vertices
) {
    cell_coords.clear();
    global_vertices.clear();

    for (uint32_t y = region_bbox.origin.y; y < region_bbox.maxY(); ++y) {
        for (uint32_t x = region_bbox.origin.x; x < region_bbox.maxX(); ++x) {
            const GridCoord coord{x, y};
            if (!layout.isPassable(coord)) {
                continue;
            }
            cell_coords.push_back(coord);
            global_vertices.push_back(layout.vertexId(coord).value);
        }
    }
}

}  // namespace

GammaOrdering buildRegionGammaFromPortIndex(
    const BrickIndex& brick_index,
    const TileSlot& parent_bbox
) {
    GammaOrdering gamma;
    for (const PortRecord& record : brick_index.ports().ports()) {
        if (!parent_bbox.contains(record.coord)) {
            continue;
        }
        if (!containsCoord(gamma.ports, record.coord)) {
            gamma.ports.push_back(record.coord);
        }
    }

    std::sort(
        gamma.ports.begin(),
        gamma.ports.end(),
        [&](const GridCoord lhs, const GridCoord rhs) noexcept {
            return compareGammaPortOrder(parent_bbox, lhs, rhs);
        }
    );

    return gamma;
}

BitMatrix buildFlatRegionPortAdjacency(
    const BrickIndex& brick_index,
    const GammaOrdering& gamma
) {
    const uint32_t gamma_size = static_cast<uint32_t>(gamma.ports.size());
    BitMatrix adjacency(gamma_size, gamma_size);
    for (uint32_t index = 0U; index < gamma_size; ++index) {
        adjacency.set(index, index);
    }

    const BrickTileIndex& tile_index = brick_index.tiles();
    for (uint32_t tile_slot = 0U; tile_slot < tile_index.decomposition().numSlots(); ++tile_slot) {
        const BaseTileSummary& summary = tile_index.summaryByIndex(tile_slot);
        if (summary.status != BaselineStatus::Completed) {
            continue;
        }

        for (uint32_t port_from = 0U; port_from < summary.numPorts(); ++port_from) {
            const uint32_t gamma_from =
                gammaIndexForCoord(gamma, summary.ports[port_from].coord);
            if (gamma_from == std::numeric_limits<uint32_t>::max()) {
                continue;
            }

            for (uint32_t port_to = 0U; port_to < summary.numPorts(); ++port_to) {
                if (!summary.boundary_summary.test(port_from, port_to)) {
                    continue;
                }
                const uint32_t gamma_to =
                    gammaIndexForCoord(gamma, summary.ports[port_to].coord);
                if (gamma_to == std::numeric_limits<uint32_t>::max()) {
                    continue;
                }
                adjacency.set(gamma_from, gamma_to);
            }
        }
    }

    for (const SeamEdge& seam_edge : brick_index.seamEdges()) {
        const PortRecord& from_record = brick_index.ports().port(seam_edge.from_port_id);
        const PortRecord& to_record = brick_index.ports().port(seam_edge.to_port_id);
        const uint32_t gamma_from = gammaIndexForCoord(gamma, from_record.coord);
        const uint32_t gamma_to = gammaIndexForCoord(gamma, to_record.coord);
        if (gamma_from == std::numeric_limits<uint32_t>::max() ||
            gamma_to == std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        adjacency.set(gamma_from, gamma_to);
    }

    return adjacency;
}

BitMatrix buildFlatRegionPortBoundarySummary(
    const BrickIndex& brick_index,
    const TileSlot& parent_bbox,
    const GammaOrdering& gamma,
    const uint64_t max_memory_bytes
) {
    const uint32_t gamma_size = static_cast<uint32_t>(gamma.ports.size());
    if (gamma_size == 0U) {
        return {};
    }
    if (!canAllocateTileReflexiveAdjacency(gamma_size, max_memory_bytes)) {
        return {};
    }

    BitMatrix closure = buildFlatRegionPortAdjacency(brick_index, gamma);
    BooleanClosure::transitiveClosureWarshallInPlace(closure);
    return projectExternalSummary(parent_bbox, gamma, closure);
}

RegionCellClosure buildRegionCellClosure(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSlot& region_bbox,
    const uint64_t max_memory_bytes
) {
    RegionCellClosure result{};
    std::vector<uint32_t> global_vertices;
    collectRegionCells(layout, region_bbox, result.cell_coords, global_vertices);

    const uint32_t num_cells = static_cast<uint32_t>(result.cell_coords.size());
    if (num_cells == 0U) {
        result.status = BaselineStatus::Completed;
        return result;
    }

    if (!canAllocateTileReflexiveAdjacency(num_cells, max_memory_bytes)) {
        result.status = BaselineStatus::SkippedByPolicy;
        return result;
    }

    const CsrGraph region_graph = buildInducedSubgraph(
        graph,
        layout,
        region_bbox,
        global_vertices
    );
    result.closure = buildTileReflexiveAdjacencyOrThrow(region_graph, max_memory_bytes);
    BooleanClosure::transitiveClosureWarshallInPlace(result.closure);
    result.status = BaselineStatus::Completed;
    return result;
}

bool boundarySummaryMatchesCellClosure(
    const RegionCellClosure& region_closure,
    const std::span<const GridCoord> exterior_ports,
    const BitMatrix& boundary_summary
) noexcept {
    if (region_closure.status != BaselineStatus::Completed) {
        return false;
    }
    if (boundary_summary.numRows() != exterior_ports.size() ||
        boundary_summary.numCols() != exterior_ports.size()) {
        return false;
    }

    for (uint32_t from = 0U; from < exterior_ports.size(); ++from) {
        const uint32_t cell_from = cellIndexForCoord(region_closure, exterior_ports[from]);
        if (cell_from == std::numeric_limits<uint32_t>::max()) {
            return false;
        }

        for (uint32_t to = 0U; to < exterior_ports.size(); ++to) {
            const uint32_t cell_to = cellIndexForCoord(region_closure, exterior_ports[to]);
            if (cell_to == std::numeric_limits<uint32_t>::max()) {
                return false;
            }

            const bool expected = region_closure.closure.test(cell_from, cell_to);
            if (boundary_summary.test(from, to) != expected) {
                return false;
            }
        }
    }

    return true;
}

}  // namespace hbrick
