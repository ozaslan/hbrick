/**
 * @file hbrick_index_builder.hpp
 * @ingroup hbrick_tile
 * @brief Incremental full H-BRICK preprocess builder with progress reporting.
 */

#pragma once

#include <cstdint>
#include <optional>

#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/hbrick_build_report.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hbrick_index.hpp"
#include "hbrick/tile/tile_decomposition.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/**
 * @brief Builds a query-ready @ref HBrickIndex in incremental steps.
 * @ingroup hbrick_tile
 *
 * Performs flat BRICK base-tile closures, port/seam indexing, hierarchy
 * construction, and bottom-up super-tile composition — the same work as
 * @ref HBrickIndex::build, but with fine-grained progress for UIs.
 */
class HBrickIndexBuilder {
public:
    /** @brief Starts a new build; resets prior state. @ingroup hbrick_tile */
    void begin(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        HBrickConfig config
    );

    /**
     * @brief Advances the build by one work unit.
     * @ingroup hbrick_tile
     * @return @c true when the build is idle, finished, or failed.
     */
    [[nodiscard]] bool step() noexcept;

    /** @brief Aborts an in-flight build. @ingroup hbrick_tile */
    void cancel() noexcept;

  /** @brief Returns whether a build is in progress. @ingroup hbrick_tile */
    [[nodiscard]] bool running() const noexcept;

    /** @brief Live progress snapshot. @ingroup hbrick_tile */
    [[nodiscard]] const HBrickBuildProgress& progress() const noexcept;

    /** @brief Report valid after the build finishes. @ingroup hbrick_tile */
    [[nodiscard]] const HBrickBuildReport& report() const noexcept;

    /**
     * @brief Moves out the built index after a successful run.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] HBrickIndex takeIndex();

private:
    void finishFailure(BaselineStatus status, const char* detail) noexcept;
    void finishSuccess() noexcept;
    void advanceWork() noexcept;

    const DirectedGridGraph* graph_ = nullptr;
    const MazeLayout* layout_ = nullptr;
    HBrickConfig config_{};
    HBrickIndex index_{};
    HBrickBuildProgress progress_{};
    HBrickBuildReport report_{};

    class BrickTileIndex tile_index_{};
    TileDecomposition decomposition_{};

    HBrickBuildStage phase_ = HBrickBuildStage::Idle;
    uint32_t next_base_tile_index_ = 0U;
    uint32_t super_level_ = 1U;
    uint32_t super_node_index_ = 0U;
    uint32_t total_super_regions_ = 0U;

    uint8_t finalize_substep_ = 0U;
    uint32_t seam_vertex_cursor_ = 0U;
    uint32_t port_graph_tile_cursor_ = 0U;
    std::optional<CsrGraphBuilder> port_graph_builder_{};

    uint64_t build_started_ns_ = 0U;
    uint64_t phase_started_ns_ = 0U;
    uint64_t base_tile_started_ns_ = 0U;
    uint64_t super_compose_started_ns_ = 0U;

    bool cancelled_ = false;
};

}  // namespace hbrick
