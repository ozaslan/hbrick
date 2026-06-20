/**
 * @file brick_panel.hpp
 * @brief BRICK tile-index panel state, build helpers, and map overlay drawing.
 */

#pragma once

#include <cstdint>
#include <optional>

#include <imgui.h>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_build_report.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hbrick_index.hpp"
#include "hbrick/tile/hbrick_index_builder.hpp"

namespace hbrick::tools {

/** @brief Minimum map zoom to draw BRICK tile boundaries and fills. */
inline constexpr float kBrickTileOverlayZoom = 1.0F;
/** @brief Minimum map zoom to draw port cell markers. */
inline constexpr float kBrickPortOverlayZoom = 5.0F;
/** @brief Minimum map zoom to draw cross-tile seam arrows. */
inline constexpr float kBrickSeamOverlayZoom = 7.0F;
/** @brief Minimum map zoom to draw tile (i,j) labels. */
inline constexpr float kBrickTileLabelZoom = 12.0F;
/** @brief Minimum map zoom to draw H-BRICK super-region outlines. */
inline constexpr float kBrickHierarchyOverlayZoom = 1.0F;
/** @brief Minimum map zoom to draw super-region (level,i,j) labels. */
inline constexpr float kBrickHierarchyLabelZoom = 6.0F;

/** @brief Which BRICK overlay layers @ref drawBrickOverlays draws. */
enum class BrickOverlayPass : uint8_t {
    Hierarchy = 0,
    TilesAndPorts = 1,
    Seams = 2,
};

struct MapPanel;

/** @brief Per-map BRICK visualization and parameter state. */
struct BrickPanelState {
    bool panel_open = false;
    bool panel_requested = false;

    uint32_t base_tile_width = 8U;
    uint32_t base_tile_height = 8U;
    uint32_t group_w = 3U;
    uint32_t group_h = 3U;
    uint32_t max_depth = kHBrickFullDepth;
    float memory_gib = 8.0F;

    bool overlay_tiles = true;
    bool overlay_ports = true;
    bool overlay_seams = true;
    bool overlay_hierarchy = true;
    /** When false (default), hide tiles whose cells are all impassable. */
    bool show_impassable_only_tiles = false;
    /**
     * Hierarchy level to visualize: @c -1 = all (L0 base + super boundaries),
     * @c 0 = L0 base tiles only, @c >= 1 = one super level's boundary cells + seams.
     */
    int32_t hierarchy_display_level = -1;

    bool index_valid = false;
    BaselineStatus index_status = BaselineStatus::NotRun;
    uint64_t graph_epoch_built = 0U;

    uint32_t num_tiles = 0U;
    uint32_t num_ports = 0U;
    uint32_t num_seams = 0U;
    uint32_t num_hierarchy_levels = 0U;

    bool build_in_progress = false;
    HBrickIndexBuilder index_builder{};
    HBrickBuildReport build_report{};

    std::optional<HBrickIndex> cached_hbrick_index{};
};

/** @brief Resets BRICK panel to defaults (map open / orientation reset). */
void resetBrickPanel(BrickPanelState& brick);

/** @brief Requests one-shot auto-open when the panel is not already open. */
void requestBrickPanelOpen(BrickPanelState& brick) noexcept;

/** @brief Clears the cached index (graph changed or user invalidated). */
void invalidateBrickIndex(BrickPanelState& brick) noexcept;

/**
 * @brief Returns whether @p brick has a valid index for @p graph_epoch.
 */
[[nodiscard]] bool brickIndexMatchesEpoch(
    const BrickPanelState& brick,
    uint64_t graph_epoch
) noexcept;

/**
 * @brief Starts a full H-BRICK preprocess build (query-ready index).
 */
void beginBrickIndexBuild(
    BrickPanelState& brick,
    const MazeLayout& layout,
    const DirectedGridGraph& graph,
    uint64_t graph_epoch
);

/**
 * @brief Advances in-flight builds; call once per frame from the app loop.
 */
void tickBrickIndexBuild(BrickPanelState& brick) noexcept;

/**
 * @brief Drops the cached index when @p graph_epoch no longer matches the build epoch.
 */
void syncBrickIndexWithGraph(BrickPanelState& brick, uint64_t graph_epoch) noexcept;

/**
 * @brief Applies pending one-shot auto-open for the BRICK window.
 */
void beginBrickPanelFrame(BrickPanelState& brick) noexcept;

/** @brief Draws the BRICK parameter / build ImGui window for one map panel. */
void drawBrickPanel(MapPanel& panel);

/**
 * @brief Draws tile, port, and/or seam overlays on the map canvas.
 *
 * Call with @ref BrickOverlayPass::Hierarchy first (under base tiles), then
 * @ref BrickOverlayPass::TilesAndPorts before maze edge arrows and
 * @ref BrickOverlayPass::Seams after them so seam arcs stay visible on top.
 */
void drawBrickOverlays(
    ImDrawList* draw_list,
    const BrickPanelState& brick,
    const MazeLayout& layout,
    float zoom,
    ImVec2 image_min,
    uint32_t x_first,
    uint32_t x_last,
    uint32_t y_first,
    uint32_t y_last,
    BrickOverlayPass pass
);

/**
 * @brief Draws L0 base-tile reachability index details for @p coord (ImGui text lines).
 *
 * No-op when the BRICK index is missing or @p coord is out of decomposition bounds.
 * @return @c true when any lines were emitted.
 */
bool drawBrickTileHoverInfo(
    const BrickPanelState& brick,
    const MazeLayout& layout,
    GridCoord coord
) noexcept;

}  // namespace hbrick::tools
