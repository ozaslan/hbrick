/**
 * @file port_graph.hpp
 * @ingroup hbrick_tile
 * @brief Global flat-BRICK port graph construction (layer C).
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/tile/seam_edge.hpp"

namespace hbrick {

class BrickTileIndex;
class PortIndex;

/**
 * @brief Collects directed seam edges between ports in different tiles.
 * @ingroup hbrick_tile
 *
 * Scans every edge in the global @p graph. An edge becomes a seam when both
 * endpoints are registered ports and belong to different tile slots.
 */
[[nodiscard]] std::vector<SeamEdge> collectSeamEdges(
    const CsrGraph& graph,
    const BrickTileIndex& tile_index,
    const PortIndex& port_index
);

/**
 * @brief Appends seam edges discovered from one half-open global-vertex range.
 * @ingroup hbrick_tile
 */
void collectSeamEdgesForVertexRange(
    const CsrGraph& graph,
    const BrickTileIndex& tile_index,
    const PortIndex& port_index,
    uint32_t vertex_begin,
    uint32_t vertex_end,
    std::vector<SeamEdge>& out
);

/**
 * @brief Adds intra-tile @c S_T shortcuts for one base tile into @p builder.
 * @ingroup hbrick_tile
 */
void addIntraTilePortEdgesForTile(
    uint32_t tile_index_value,
    const BrickTileIndex& tile_index,
    const PortIndex& port_index,
    CsrGraphBuilder& builder
);

/**
 * @brief Appends cross-tile seam edges to @p builder.
 * @ingroup hbrick_tile
 */
void addSeamEdgesToPortGraph(
    CsrGraphBuilder& builder,
    std::span<const SeamEdge> seam_edges
);

/**
 * @brief Builds the global port-graph CSR from intra-tile @c S_T shortcuts and seams.
 * @ingroup hbrick_tile
 *
 * For each @c S_T[p,q]=1 in a base tile, adds one compressed edge between the
 * corresponding global port ids. Appends every @p seam_edge afterward.
 */
[[nodiscard]] CsrGraph buildPortGraphCsr(
    const BrickTileIndex& tile_index,
    const PortIndex& port_index,
    std::span<const SeamEdge> seam_edges
);

}  // namespace hbrick
