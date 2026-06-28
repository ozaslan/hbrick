/**
 * @file brick_tile_index.hpp
 * @ingroup hbrick_tile
 * @brief All base tile summaries for a decomposed map plus global vertex lookups.
 */

#pragma once

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/tile_decomposition.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;
class HBrickIndexBuilder;
class BrickIndexBuilder;

/**
 * @brief Level-0 BRICK tile index: one @ref BaseTileSummary per decomposition slot.
 * @ingroup hbrick_tile
 */
class BrickTileIndex {
public:
    /**
     * @brief Builds summaries for every slot in @p decomposition.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] static BrickTileIndex build(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        const TileDecomposition& decomposition,
        uint64_t max_memory_bytes
    );

    /** @brief Map decomposition used to build this index. @ingroup hbrick_tile */
    [[nodiscard]] const TileDecomposition& decomposition() const noexcept {
        return decomposition_;
    }

    /** @brief Summary for slot (@p tile_i, @p tile_j). @ingroup hbrick_tile */
    [[nodiscard]] const BaseTileSummary& summary(
        uint32_t tile_i,
        uint32_t tile_j
    ) const noexcept;

    /** @brief Summary with linear slot @p index. @ingroup hbrick_tile */
    [[nodiscard]] const BaseTileSummary& summaryByIndex(uint32_t index) const noexcept;

    /** @brief Read-only view of all slot summaries. @ingroup hbrick_tile */
    [[nodiscard]] std::span<const BaseTileSummary> summaries() const noexcept {
        return summaries_;
    }

    /**
     * @brief Linear decomposition slot index for a global vertex.
     * @ingroup hbrick_tile
     *
     * @return Slot index, or @c UINT32_MAX when @p global_vertex is out of range.
     */
    [[nodiscard]] uint32_t tileIndexForGlobalVertex(uint32_t global_vertex) const noexcept;

    /**
     * @brief Local vertex index inside the owning tile summary.
     * @ingroup hbrick_tile
     *
     * @return Local index, or @c UINT32_MAX when the vertex is blocked or unmapped.
     */
    [[nodiscard]] uint32_t localIndexForGlobalVertex(uint32_t global_vertex) const noexcept;

    /** @brief Returns whether every slot summary completed successfully. @ingroup hbrick_tile */
    [[nodiscard]] bool allTilesCompleted() const noexcept;

private:
    friend class HBrickIndexBuilder;
    friend class BrickIndexBuilder;

    TileDecomposition decomposition_{};
    std::vector<BaseTileSummary> summaries_{};
    std::vector<uint32_t> global_vertex_tile_index_{};
    std::vector<uint32_t> global_vertex_local_index_{};
};

}  // namespace hbrick
