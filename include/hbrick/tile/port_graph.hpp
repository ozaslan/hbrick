/**
 * @file port_graph.hpp
 * @ingroup hbrick_tile
 * @brief Global flat-BRICK port graph construction (layer C).
 */

#pragma once

#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/tile/seam_edge.hpp"

namespace hbrick {

class BrickTileIndex;
class PortIndex;
class PreprocessMemoryLedger;

/**
 * @brief Tracks unique directed port-port seam keys during incremental collection.
 * @ingroup hbrick_tile
 */
class SeamEdgeDeduper {
public:
    /** @brief Clears all previously seen seam keys. @ingroup hbrick_tile */
    void clear() noexcept { keys_.clear(); }

    /**
     * @brief Inserts @c (from_port_id, to_port_id) if not seen yet.
     * @ingroup hbrick_tile
     * @return @c true when the key is new.
     */
    [[nodiscard]] bool tryInsert(
        uint32_t from_port_id,
        uint32_t to_port_id
    ) noexcept {
        const uint64_t key =
            (static_cast<uint64_t>(from_port_id) << 32U)
            | static_cast<uint64_t>(to_port_id);
        return keys_.insert(key).second;
    }

private:
    std::unordered_set<uint64_t> keys_;
};

/**
 * @brief Appends seam edges discovered from one half-open global-vertex range.
 * @ingroup hbrick_tile
 *
 * When @p deduper is non-null, each unique @c (from_port_id, to_port_id) pair is
 * appended at most once across all incremental calls sharing the same deduper.
 *
 * When @p ledger is non-null, returns @c false if charging a new seam would exceed
 * the ledger cap (no seam is appended in that case).
 */
[[nodiscard]] bool collectSeamEdgesForVertexRange(
    const CsrGraph& graph,
    const BrickTileIndex& tile_index,
    const PortIndex& port_index,
    uint32_t vertex_begin,
    uint32_t vertex_end,
    std::vector<SeamEdge>& out,
    SeamEdgeDeduper* deduper = nullptr,
    PreprocessMemoryLedger* ledger = nullptr
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

}  // namespace hbrick
