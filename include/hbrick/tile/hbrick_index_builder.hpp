/**
 * @file hbrick_index_builder.hpp
 * @ingroup hbrick_tile
 * @brief Incremental full H-BRICK preprocess builder with progress reporting.
 */

#pragma once

#include <cstdint>
#include <optional>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/tile/brick_index_builder.hpp"
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
 */
class HBrickIndexBuilder {
public:
    void begin(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        HBrickConfig config
    );

    [[nodiscard]] bool step() noexcept;

    void cancel() noexcept;

    [[nodiscard]] bool running() const noexcept;

    [[nodiscard]] const HBrickBuildProgress& progress() const noexcept;

    [[nodiscard]] const HBrickBuildReport& report() const noexcept;

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

    BrickIndexBuilder brick_index_builder_{};
    TileDecomposition decomposition_{};

    HBrickBuildStage phase_ = HBrickBuildStage::Idle;
    uint32_t super_level_ = 1U;
    uint32_t super_node_index_ = 0U;
    uint32_t total_super_regions_ = 0U;

    uint64_t build_started_ns_ = 0U;
    uint64_t phase_started_ns_ = 0U;
    uint64_t super_compose_started_ns_ = 0U;

    BitMatrix base_tile_closure_scratch_{};

    bool cancelled_ = false;
};

}  // namespace hbrick
