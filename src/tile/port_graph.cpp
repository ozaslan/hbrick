#include "hbrick/tile/port_graph.hpp"

#include <limits>

#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/port_index.hpp"

namespace hbrick {

std::vector<SeamEdge> collectSeamEdges(
    const CsrGraph& graph,
    const BrickTileIndex& tile_index,
    const PortIndex& port_index
) {
    std::vector<SeamEdge> seam_edges;

    for (uint32_t global_from = 0U; global_from < graph.numVertices(); ++global_from) {
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

            seam_edges.push_back(SeamEdge{from_port_id, to_port_id});
        }
    }

    return seam_edges;
}

CsrGraph buildPortGraphCsr(
    const BrickTileIndex& tile_index,
    const PortIndex& port_index,
    const std::span<const SeamEdge> seam_edges
) {
    CsrGraphBuilder builder{port_index.numPorts()};

    for (uint32_t tile_index_value = 0U;
         tile_index_value < tile_index.decomposition().numSlots();
         ++tile_index_value) {
        const BaseTileSummary& summary = tile_index.summaryByIndex(tile_index_value);
        if (summary.status != BaselineStatus::Completed) {
            continue;
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

    for (const SeamEdge& seam_edge : seam_edges) {
        builder.addEdge(seam_edge.from_port_id, seam_edge.to_port_id);
    }

    return builder.build();
}

}  // namespace hbrick
