/**
 * @file brick_index.hpp
 * @ingroup hbrick_tile
 * @brief Complete flat-BRICK preprocess product: tiles, ports, and port graph.
 */

#pragma once

#include <cstdint>

#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/port_index.hpp"
#include "hbrick/tile/seam_edge.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;
class HBrickIndexBuilder;
class BrickIndexBuilder;

/**
 * @brief Flat BRICK index owning base tiles, the global port index, and port CSR.
 * @ingroup hbrick_tile
 */
class BrickIndex {
public:
    /**
     * @brief Builds the full flat-BRICK preprocess product for a map.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] static BrickIndex build(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        TileSize nominal_tile_size,
        uint64_t max_memory_bytes
    );

    /** @brief Outcome of building this index. @ingroup hbrick_tile */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

    /** @brief Base tile summaries and global vertex lookups. @ingroup hbrick_tile */
    [[nodiscard]] const BrickTileIndex& tiles() const noexcept { return tile_index_; }

    /** @brief Global port id bijections. @ingroup hbrick_tile */
    [[nodiscard]] const PortIndex& ports() const noexcept { return port_index_; }

    /** @brief Compressed global port graph CSR. @ingroup hbrick_tile */
    [[nodiscard]] const CsrGraph& portGraph() const noexcept { return port_graph_; }

    /** @brief Cross-tile seam edges used to build @ref portGraph. @ingroup hbrick_tile */
    [[nodiscard]] const std::vector<SeamEdge>& seamEdges() const noexcept {
        return seam_edges_;
    }

private:
    friend class HBrickIndexBuilder;
    friend class BrickIndexBuilder;

    BaselineStatus status_ = BaselineStatus::NotRun;
    BrickTileIndex tile_index_{};
    PortIndex port_index_{};
    CsrGraph port_graph_{};
    std::vector<SeamEdge> seam_edges_{};
};

}  // namespace hbrick
