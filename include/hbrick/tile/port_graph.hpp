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
