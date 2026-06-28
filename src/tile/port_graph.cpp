#include "hbrick/tile/port_graph.hpp"

#include <algorithm>
#include <limits>

#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/port_index.hpp"

namespace hbrick {

void collectSeamEdgesForVertexRange(
    const CsrGraph& graph,
    const BrickTileIndex& tile_index,
    const PortIndex& port_index,
    const uint32_t vertex_begin,
    const uint32_t vertex_end,
    std::vector<SeamEdge>& out
) {
    const uint32_t end = std::min(vertex_end, graph.numVertices());
    for (uint32_t global_from = vertex_begin; global_from < end; ++global_from) {
        const uint32_t from_port_id = port_index.portIdForGlobalVertex(global_from);
        if (from_port_id == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        const uint32_t from_tile_index = tile_index.tileIndexForGlobalVertex(global_from);
        for (const uint32_t global_to : graph.outNeighbors(global_from)) {
            const uint32_t to_port_id = port_index.portIdForGlobalVertex(global_to);
            if (to_port_id == std::numeric_limits<uint32_t>::max()) {
                continue;
            }

            const uint32_t to_tile_index = tile_index.tileIndexForGlobalVertex(global_to);
            if (from_tile_index == to_tile_index) {
                continue;
            }

            out.push_back(SeamEdge{from_port_id, to_port_id});
        }
    }
}

void addIntraTilePortEdgesForTile(
    const uint32_t tile_index_value,
    const BrickTileIndex& tile_index,
    const PortIndex& port_index,
    CsrGraphBuilder& builder
) {
    const BaseTileSummary& summary = tile_index.summaryByIndex(tile_index_value);
    if (summary.status != BaselineStatus::Completed) {
        return;
    }

    for (uint32_t port_from = 0U; port_from < summary.numPorts(); ++port_from) {
        const uint32_t global_from_port_id =
            port_index.portIdForTilePort(tile_index_value, port_from);
        if (global_from_port_id == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        for (uint32_t port_to = 0U; port_to < summary.numPorts(); ++port_to) {
            if (!summary.boundary_summary.test(port_from, port_to)) {
                continue;
            }

            const uint32_t global_to_port_id =
                port_index.portIdForTilePort(tile_index_value, port_to);
            if (global_to_port_id == std::numeric_limits<uint32_t>::max()) {
                continue;
            }

            if (global_from_port_id != global_to_port_id) {
                builder.addEdge(global_from_port_id, global_to_port_id);
            }
        }
    }
}

void addSeamEdgesToPortGraph(
    CsrGraphBuilder& builder,
    const std::span<const SeamEdge> seam_edges
) {
    for (const SeamEdge& seam_edge : seam_edges) {
        builder.addEdge(seam_edge.from_port_id, seam_edge.to_port_id);
    }
}

}  // namespace hbrick
