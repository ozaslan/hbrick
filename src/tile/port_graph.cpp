#include "hbrick/tile/port_graph.hpp"

#include <algorithm>
#include <bit>
#include <limits>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/port_index.hpp"
#include "hbrick/tile/preprocess_memory_ledger.hpp"

namespace hbrick {

namespace {

template <typename Callback>
void forEachSetColumnInRow(
    const BitMatrix& matrix,
    const uint32_t row,
    Callback&& callback
) {
    const uint32_t num_cols = matrix.numCols();
    const BitVector& row_bits = matrix.row(row);
    const size_t num_words = row_bits.numWords();
    const size_t full_words = static_cast<size_t>(num_cols) / 64U;
    const uint32_t tail_bits = num_cols % 64U;
    const uint64_t tail_mask =
        tail_bits == 0U ? ~0ULL : ((1ULL << tail_bits) - 1ULL);

    for (size_t word_index = 0U; word_index < num_words; ++word_index) {
        uint64_t bits = row_bits.word(word_index);
        if (word_index == full_words && tail_bits != 0U) {
            bits &= tail_mask;
        }

        while (bits != 0U) {
            const uint32_t col = static_cast<uint32_t>(
                word_index * 64U + static_cast<size_t>(std::countr_zero(bits))
            );
            callback(col);
            bits &= bits - 1U;
        }
    }
}

}  // namespace

[[nodiscard]] bool collectSeamEdgesForVertexRange(
    const CsrGraph& graph,
    const BrickTileIndex& tile_index,
    const PortIndex& port_index,
    const uint32_t vertex_begin,
    const uint32_t vertex_end,
    std::vector<SeamEdge>& out,
    SeamEdgeDeduper* deduper,
    PreprocessMemoryLedger* ledger
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

            if (deduper != nullptr && !deduper->tryInsert(from_port_id, to_port_id)) {
                continue;
            }

            if (ledger != nullptr && !ledger->tryCharge(sizeof(SeamEdge))) {
                return false;
            }

            out.push_back(SeamEdge{from_port_id, to_port_id});
        }
    }

    return true;
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

    const uint32_t num_ports = summary.numPorts();
    SeamEdgeDeduper edge_deduper;

    for (uint32_t port_from = 0U; port_from < num_ports; ++port_from) {
        const uint32_t global_from_port_id =
            port_index.portIdForTilePort(tile_index_value, port_from);
        if (global_from_port_id == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        forEachSetColumnInRow(
            summary.boundary_summary,
            port_from,
            [&](const uint32_t port_to) {
                if (port_from == port_to) {
                    return;
                }

                const uint32_t global_to_port_id =
                    port_index.portIdForTilePort(tile_index_value, port_to);
                if (global_to_port_id == std::numeric_limits<uint32_t>::max()) {
                    return;
                }
                if (global_from_port_id == global_to_port_id) {
                    return;
                }
                if (!edge_deduper.tryInsert(global_from_port_id, global_to_port_id)) {
                    return;
                }

                builder.addEdge(global_from_port_id, global_to_port_id);
            }
        );
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
