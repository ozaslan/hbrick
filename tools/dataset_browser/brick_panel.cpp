#include "brick_panel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/external_boundary_ports.hpp"
#include "hbrick/tile/group_size.hpp"
#include "hbrick/tile/hbrick_build_format.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/port_index.hpp"
#include "hbrick/tile/region_node.hpp"
#include "hbrick/tile/seam_edge.hpp"
#include "hbrick/tile/tile_port.hpp"
#include "hbrick/tile/tile_port_record.hpp"
#include "hbrick/tile/tile_decomposition.hpp"
#include "hbrick/tile/tile_size.hpp"
#include "hbrick/tile/tile_slot.hpp"

#include "browser_app.hpp"

namespace hbrick::tools {

namespace {

[[nodiscard]] const char* baselineStatusLabel(const BaselineStatus status) noexcept {
    switch (status) {
        case BaselineStatus::Completed:
            return "Completed";
        case BaselineStatus::SkippedByPolicy:
            return "SkippedByPolicy";
        case BaselineStatus::Failed:
            return "Failed";
        case BaselineStatus::NotRun:
        default:
            return "NotRun";
    }
}

[[nodiscard]] uint64_t memoryBytesFromGib(const float memory_gib) noexcept {
    return static_cast<uint64_t>(
        static_cast<double>(std::max(0.0F, memory_gib)) * (1024.0 * 1024.0 * 1024.0)
    );
}

[[nodiscard]] float clampZoomStroke(const float zoom, const float factor) noexcept {
    return std::clamp(zoom * factor, 1.0F, 4.0F);
}

[[nodiscard]] bool tileHasPassableCell(
    const MazeLayout& layout,
    const GridCoord origin,
    const GridDimensions extent
) noexcept {
    const uint32_t x_end = origin.x + extent.width;
    const uint32_t y_end = origin.y + extent.height;
    for (uint32_t y = origin.y; y < y_end; ++y) {
        for (uint32_t x = origin.x; x < x_end; ++x) {
            if (layout.isPassable(x, y)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool tileHasPassableCell(
    const MazeLayout& layout,
    const TileSlot& slot
) noexcept {
    return tileHasPassableCell(layout, slot.origin, slot.extent);
}

[[nodiscard]] bool shouldDrawTileOverlay(
    const BrickPanelState& brick,
    const MazeLayout& layout,
    const TileSlot& slot
) noexcept {
    return brick.show_impassable_only_tiles || tileHasPassableCell(layout, slot);
}

[[nodiscard]] bool regionHasPassableBaseTile(
    const MazeLayout& layout,
    const HierarchyTree& hierarchy,
    const RegionNode& region
) noexcept {
    if (region.id.level == 0U) {
        return tileHasPassableCell(layout, region.origin, region.extent);
    }
    for (const RegionNodeId& child_id : region.children) {
        const RegionNode& child = hierarchy.node(child_id.level, child_id.index);
        if (regionHasPassableBaseTile(layout, hierarchy, child)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool shouldDrawSuperRegion(
    const BrickPanelState& brick,
    const MazeLayout& layout,
    const HierarchyTree& hierarchy,
    const RegionNode& region
) noexcept {
    return brick.show_impassable_only_tiles
        || regionHasPassableBaseTile(layout, hierarchy, region);
}

[[nodiscard]] TileSlot tileSlotFromRegion(const RegionNode& region) noexcept {
    TileSlot slot{};
    slot.tile_i = region.region_i;
    slot.tile_j = region.region_j;
    slot.origin = region.origin;
    slot.extent = region.extent;
    return slot;
}

[[nodiscard]] bool coordOnRegionBoundary(
    const TileSlot& slot,
    const GridCoord coord
) noexcept {
    if (coord.x < slot.origin.x || coord.y < slot.origin.y) {
        return false;
    }
    if (coord.x >= slot.maxX() || coord.y >= slot.maxY()) {
        return false;
    }
    return coord.y == slot.origin.y
        || coord.y + 1U == slot.maxY()
        || coord.x == slot.origin.x
        || coord.x + 1U == slot.maxX();
}

[[nodiscard]] std::optional<RegionNodeId> ancestorRegionAtLevel(
    const HierarchyTree& hierarchy,
    const uint32_t base_tile_index,
    const uint32_t target_level
) noexcept {
    if (target_level == 0U) {
        return RegionNodeId{0U, base_tile_index};
    }

    RegionNodeId current{0U, base_tile_index};
    while (current.level < target_level) {
        const RegionNode& node = hierarchy.node(current.level, current.index);
        if (!node.has_parent) {
            return std::nullopt;
        }
        current = node.parent;
    }
    if (current.level != target_level) {
        return std::nullopt;
    }
    return current;
}

[[nodiscard]] bool sameRegionId(
    const RegionNodeId lhs,
    const RegionNodeId rhs
) noexcept {
    return lhs.level == rhs.level && lhs.index == rhs.index;
}

[[nodiscard]] ImU32 hierarchyOutlineColor(const uint32_t level) noexcept {
    switch (level) {
        case 1U:
            return IM_COL32(255, 100, 210, 220);
        case 2U:
            return IM_COL32(255, 220, 70, 220);
        case 3U:
            return IM_COL32(110, 255, 140, 220);
        default:
            return IM_COL32(255, 150, 80, 220);
    }
}

[[nodiscard]] ImU32 withAlpha(const ImU32 color, const uint8_t alpha) noexcept {
    return (color & 0x00FFFFFFU) | (static_cast<ImU32>(alpha) << 24U);
}

[[nodiscard]] ImU32 tileFillColor(const uint32_t tile_i, const uint32_t tile_j) noexcept {
    const bool alt = ((tile_i + tile_j) % 2U) == 0U;
    return alt ? IM_COL32(70, 140, 220, 38) : IM_COL32(220, 130, 60, 34);
}

[[nodiscard]] ImU32 tileOutlineColor(const uint32_t tile_i, const uint32_t tile_j) noexcept {
    const bool alt = ((tile_i + tile_j) % 2U) == 0U;
    return alt ? IM_COL32(100, 190, 255, 210) : IM_COL32(255, 175, 90, 210);
}

[[nodiscard]] ImU32 portColorForSide(const PortSide side) noexcept {
    switch (side) {
        case PortSide::North:
            return IM_COL32(90, 220, 255, 230);
        case PortSide::East:
            return IM_COL32(255, 220, 90, 230);
        case PortSide::South:
            return IM_COL32(255, 120, 200, 230);
        case PortSide::West:
            return IM_COL32(120, 255, 150, 230);
    }
    return IM_COL32(200, 200, 200, 230);
}

[[nodiscard]] ImVec2 cellCenter(
    const ImVec2 image_min,
    const float zoom,
    const GridCoord coord
) noexcept {
    return ImVec2(
        image_min.x + (static_cast<float>(coord.x) + 0.5F) * zoom,
        image_min.y + (static_cast<float>(coord.y) + 0.5F) * zoom
    );
}

void drawSeamArrow(
    ImDrawList* draw_list,
    const ImVec2 from_center,
    const ImVec2 to_center,
    const float zoom,
    const float head_size
) {
    const float dx = to_center.x - from_center.x;
    const float dy = to_center.y - from_center.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5F) {
        return;
    }

    const float ux = dx / len;
    const float uy = dy / len;

    // Run seam arcs in a parallel lane beside maze adjacency arrows (cell centers).
    const float lane = std::clamp(zoom * 0.26F, 3.0F, 14.0F);
    const float perp_x = -uy * lane;
    const float perp_y = ux * lane;

    constexpr float kEndpointShrink = 0.30F;
    const float inset = len * kEndpointShrink;
    const ImVec2 from(
        from_center.x + ux * inset + perp_x,
        from_center.y + uy * inset + perp_y
    );
    const ImVec2 to(
        to_center.x - ux * inset + perp_x,
        to_center.y - uy * inset + perp_y
    );

    const ImU32 core = IM_COL32(0, 255, 255, 255);
    const ImU32 stroke_color = IM_COL32(0, 0, 0, 255);
    const float thickness = std::clamp(zoom * 0.10F, 1.8F, 3.0F);
    const float head = std::clamp(head_size, 4.0F, 12.0F);

    const float seg_dx = to.x - from.x;
    const float seg_dy = to.y - from.y;
    const float seg_len = std::sqrt(seg_dx * seg_dx + seg_dy * seg_dy);
    if (seg_len < 1.0F) {
        return;
    }

    const float nx = seg_dx / seg_len;
    const float ny = seg_dy / seg_len;
    const ImVec2 tip = to;

    const auto wing = [&](const float head_len) {
        return std::pair<ImVec2, ImVec2>{
            ImVec2(
                tip.x - nx * head_len - ny * head_len * 0.55F,
                tip.y - ny * head_len + nx * head_len * 0.55F
            ),
            ImVec2(
                tip.x - nx * head_len + ny * head_len * 0.55F,
                tip.y - ny * head_len - nx * head_len * 0.55F
            )
        };
    };

    const auto [stroke_left, stroke_right] = wing(head * 1.22F);
    const auto [core_left, core_right] = wing(head);

    // Stroke behind core: shaft, then enlarged head, then core on top.
    draw_list->AddLine(from, tip, stroke_color, thickness + 1.6F);
    draw_list->AddTriangleFilled(tip, stroke_left, stroke_right, stroke_color);

    const ImVec2 shaft_end(
        tip.x - nx * head * 0.45F,
        tip.y - ny * head * 0.45F
    );
    draw_list->AddLine(from, shaft_end, core, thickness);
    draw_list->AddTriangleFilled(tip, core_left, core_right, core);
}

void drawSuperBoundaryMarker(
    ImDrawList* draw_list,
    const ImVec2 center,
    const float zoom,
    const ImU32 color
) {
    const float radius = std::clamp(zoom * 0.26F, 2.5F, 8.0F);
    const float ring = std::clamp(zoom * 0.36F, 3.5F, 10.0F);
    draw_list->AddCircleFilled(center, ring + 1.0F, IM_COL32(0, 0, 0, 60));
    draw_list->AddCircle(center, ring, withAlpha(color, 150U), 0, 1.8F);
    draw_list->AddCircleFilled(center, radius, color);
}

void drawSuperRegionLabel(
    ImDrawList* draw_list,
    const RegionNode& region,
    const ImVec2 image_min,
    const float zoom
) {
    const ImVec2 p0(
        image_min.x + static_cast<float>(region.origin.x) * zoom,
        image_min.y + static_cast<float>(region.origin.y) * zoom
    );
    const ImVec2 p1(
        image_min.x + static_cast<float>(region.origin.x + region.extent.width) * zoom,
        image_min.y + static_cast<float>(region.origin.y + region.extent.height) * zoom
    );

    char label[24];
    std::snprintf(
        label,
        sizeof(label),
        "L%u %u,%u",
        region.id.level,
        region.region_i,
        region.region_j
    );
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const ImVec2 center(
        (p0.x + p1.x) * 0.5F - text_size.x * 0.5F,
        (p0.y + p1.y) * 0.5F - text_size.y * 0.5F
    );
    const ImVec2 bg0(center.x - 2.0F, center.y - 1.0F);
    const ImVec2 bg1(center.x + text_size.x + 2.0F, center.y + text_size.y + 1.0F);
    draw_list->AddRectFilled(bg0, bg1, IM_COL32(18, 10, 24, 175), 2.0F);
    draw_list->AddText(center, IM_COL32(245, 230, 255, 230), label);
}

void drawSuperTileSlot(
    ImDrawList* draw_list,
    const RegionNode& region,
    const ImVec2 image_min,
    const float zoom,
    const bool show_label
) {
    const TileSlot slot = tileSlotFromRegion(region);
    const ImVec2 p0(
        image_min.x + static_cast<float>(slot.origin.x) * zoom,
        image_min.y + static_cast<float>(slot.origin.y) * zoom
    );
    const ImVec2 p1(
        image_min.x + static_cast<float>(slot.maxX()) * zoom,
        image_min.y + static_cast<float>(slot.maxY()) * zoom
    );

    const ImU32 fill = tileFillColor(slot.tile_i, slot.tile_j);
    const ImU32 outline = tileOutlineColor(slot.tile_i, slot.tile_j);
    const float stroke = clampZoomStroke(zoom, 0.07F);

    draw_list->AddRectFilled(p0, p1, fill);
    draw_list->AddRect(p0, p1, withAlpha(IM_COL32(0, 0, 0, 255), 70U), 0.0F, 0, stroke + 1.2F);
    draw_list->AddRect(p0, p1, outline, 0.0F, 0, stroke);

    if (!show_label) {
        return;
    }

    char label[24];
    std::snprintf(
        label,
        sizeof(label),
        "L%u %u,%u",
        region.id.level,
        region.region_i,
        region.region_j
    );
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const ImVec2 center(
        (p0.x + p1.x) * 0.5F - text_size.x * 0.5F,
        (p0.y + p1.y) * 0.5F - text_size.y * 0.5F
    );
    const ImVec2 bg0(center.x - 2.0F, center.y - 1.0F);
    const ImVec2 bg1(center.x + text_size.x + 2.0F, center.y + text_size.y + 1.0F);
    draw_list->AddRectFilled(bg0, bg1, IM_COL32(12, 12, 18, 170), 2.0F);
    draw_list->AddText(center, IM_COL32(230, 235, 245, 220), label);
}

void drawSuperRegionPortCells(
    ImDrawList* draw_list,
    const MazeLayout& layout,
    const RegionNode& region,
    const ImVec2 image_min,
    const float zoom,
    const uint32_t x_first,
    const uint32_t x_last,
    const uint32_t y_first,
    const uint32_t y_last
) {
    const TileSlot slot = tileSlotFromRegion(region);
    const ImU32 marker_color = hierarchyOutlineColor(region.id.level);

    for (const GridCoord& coord : externalBoundaryPorts(slot)) {
        if (!layout.isPassable(coord)) {
            continue;
        }
        if (coord.x < x_first || coord.x >= x_last
            || coord.y < y_first || coord.y >= y_last) {
            continue;
        }
        drawSuperBoundaryMarker(
            draw_list,
            cellCenter(image_min, zoom, coord),
            zoom,
            marker_color
        );
    }
}

void drawSuperRegionOverlays(
    ImDrawList* draw_list,
    const BrickPanelState& brick,
    const MazeLayout& layout,
    const HierarchyTree& hierarchy,
    const RegionNode& region,
    const ImVec2 image_min,
    const float zoom,
    const bool show_labels,
    const uint32_t x_first,
    const uint32_t x_last,
    const uint32_t y_first,
    const uint32_t y_last
) {
    if (!shouldDrawSuperRegion(brick, layout, hierarchy, region)) {
        return;
    }

    if (brick.overlay_tiles) {
        drawSuperTileSlot(draw_list, region, image_min, zoom, show_labels);
    }
    const bool draw_port_cells = brick.overlay_ports
        || (brick.overlay_hierarchy && !brick.overlay_tiles);
    if (draw_port_cells) {
        drawSuperRegionPortCells(
            draw_list,
            layout,
            region,
            image_min,
            zoom,
            x_first,
            x_last,
            y_first,
            y_last
        );
    }
    if (show_labels && brick.overlay_hierarchy && !brick.overlay_tiles) {
        drawSuperRegionLabel(draw_list, region, image_min, zoom);
    }
}

void drawSuperLevelSeams(
    ImDrawList* draw_list,
    const BrickPanelState& brick,
    const MazeLayout& layout,
    const HierarchyTree& hierarchy,
    const BrickIndex& index,
    const uint32_t level,
    const ImVec2 image_min,
    const float zoom,
    const uint32_t x_first,
    const uint32_t x_last,
    const uint32_t y_first,
    const uint32_t y_last
) {
    if (level == 0U || level >= hierarchy.numLevels()) {
        return;
    }

    const float head = std::clamp(zoom * 0.28F, 5.0F, 12.0F);
    const PortIndex& ports = index.ports();

    for (const SeamEdge& seam : index.seamEdges()) {
        const PortRecord& from = ports.port(seam.from_port_id);
        const PortRecord& to = ports.port(seam.to_port_id);

        const std::optional<RegionNodeId> from_region =
            ancestorRegionAtLevel(hierarchy, from.tile_index, level);
        const std::optional<RegionNodeId> to_region =
            ancestorRegionAtLevel(hierarchy, to.tile_index, level);
        if (!from_region.has_value() || !to_region.has_value()) {
            continue;
        }
        if (sameRegionId(*from_region, *to_region)) {
            continue;
        }

        const RegionNode& from_node = hierarchy.node(from_region->level, from_region->index);
        const RegionNode& to_node = hierarchy.node(to_region->level, to_region->index);
        if (!shouldDrawSuperRegion(brick, layout, hierarchy, from_node)
            || !shouldDrawSuperRegion(brick, layout, hierarchy, to_node)) {
            continue;
        }

        const TileSlot from_slot = tileSlotFromRegion(from_node);
        const TileSlot to_slot = tileSlotFromRegion(to_node);
        if (!coordOnRegionBoundary(from_slot, from.coord)
            || !coordOnRegionBoundary(to_slot, to.coord)) {
            continue;
        }

        if (from.coord.x < x_first && to.coord.x < x_first) {
            continue;
        }
        if (from.coord.x >= x_last && to.coord.x >= x_last) {
            continue;
        }
        if (from.coord.y < y_first && to.coord.y < y_first) {
            continue;
        }
        if (from.coord.y >= y_last && to.coord.y >= y_last) {
            continue;
        }

        drawSeamArrow(
            draw_list,
            cellCenter(image_min, zoom, from.coord),
            cellCenter(image_min, zoom, to.coord),
            zoom,
            head
        );
    }
}

void drawTileSlot(
    ImDrawList* draw_list,
    const TileSlot& slot,
    const ImVec2 image_min,
    const float zoom,
    const bool show_label
) {
    const ImVec2 p0(
        image_min.x + static_cast<float>(slot.origin.x) * zoom,
        image_min.y + static_cast<float>(slot.origin.y) * zoom
    );
    const ImVec2 p1(
        image_min.x + static_cast<float>(slot.maxX()) * zoom,
        image_min.y + static_cast<float>(slot.maxY()) * zoom
    );

    const ImU32 fill = tileFillColor(slot.tile_i, slot.tile_j);
    const ImU32 outline = tileOutlineColor(slot.tile_i, slot.tile_j);
    const float stroke = clampZoomStroke(zoom, 0.07F);

    draw_list->AddRectFilled(p0, p1, fill);
    draw_list->AddRect(p0, p1, withAlpha(IM_COL32(0, 0, 0, 255), 70U), 0.0F, 0, stroke + 1.2F);
    draw_list->AddRect(p0, p1, outline, 0.0F, 0, stroke);

    if (!show_label) {
        return;
    }

    char label[16];
    std::snprintf(label, sizeof(label), "%u,%u", slot.tile_i, slot.tile_j);
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const ImVec2 center(
        (p0.x + p1.x) * 0.5F - text_size.x * 0.5F,
        (p0.y + p1.y) * 0.5F - text_size.y * 0.5F
    );
    const ImVec2 bg0(center.x - 2.0F, center.y - 1.0F);
    const ImVec2 bg1(center.x + text_size.x + 2.0F, center.y + text_size.y + 1.0F);
    draw_list->AddRectFilled(bg0, bg1, IM_COL32(12, 12, 18, 170), 2.0F);
    draw_list->AddText(center, IM_COL32(230, 235, 245, 220), label);
}

void drawPortMarker(
    ImDrawList* draw_list,
    const ImVec2 center,
    const PortSide side,
    const float zoom
) {
    const ImU32 color = portColorForSide(side);
    const float radius = std::clamp(zoom * 0.22F, 2.0F, 7.0F);
    const float ring = std::clamp(zoom * 0.32F, 3.0F, 9.0F);

    draw_list->AddCircleFilled(center, ring + 1.0F, IM_COL32(0, 0, 0, 55));
    draw_list->AddCircle(center, ring, withAlpha(color, 140U), 0, 1.5F);
    draw_list->AddCircleFilled(center, radius, color);
    draw_list->AddCircleFilled(center, std::max(1.0F, radius * 0.35F), IM_COL32(255, 255, 255, 200));
}

}  // namespace

void resetBrickPanel(BrickPanelState& brick) {
    brick = BrickPanelState{};
}

void requestBrickPanelOpen(BrickPanelState& brick) noexcept {
    if (!brick.panel_open) {
        brick.panel_requested = true;
    }
}

void invalidateBrickIndex(BrickPanelState& brick) noexcept {
    brick.index_valid = false;
    brick.index_status = BaselineStatus::NotRun;
    brick.build_in_progress = false;
    brick.index_builder = HBrickIndexBuilder{};
    brick.build_report = HBrickBuildReport{};
    brick.cached_hbrick_index.reset();
    brick.num_tiles = 0U;
    brick.num_ports = 0U;
    brick.num_seams = 0U;
    brick.num_hierarchy_levels = 0U;
}

void syncBrickIndexWithGraph(BrickPanelState& brick, const uint64_t graph_epoch) noexcept {
    if (brick.index_valid && brick.graph_epoch_built != graph_epoch) {
        invalidateBrickIndex(brick);
    }
}

bool brickIndexMatchesEpoch(
    const BrickPanelState& brick,
    const uint64_t graph_epoch
) noexcept {
    return brick.index_valid
        && brick.cached_hbrick_index.has_value()
        && brick.graph_epoch_built == graph_epoch;
}

namespace {

void applyBuildReportCounts(BrickPanelState& brick) noexcept {
    const HBrickBuildReport& report = brick.build_report;
    brick.num_tiles = report.num_base_tiles;
    brick.num_ports = report.num_ports;
    brick.num_seams = report.num_seams;
    brick.num_hierarchy_levels = report.num_hierarchy_levels;
}

HBrickConfig makeHBrickConfig(const BrickPanelState& brick) noexcept {
    HBrickConfig config{};
    config.base_tile_size = TileSize{brick.base_tile_width, brick.base_tile_height};
    config.group_size = GroupSize{brick.group_w, brick.group_h};
    config.max_depth = brick.max_depth;
    config.max_memory_bytes = kHBrickUnlimitedMemoryBytes;
    return config;
}

void drawIndexStorageEstimate(const HBrickStorageEstimate& storage) {
    char bytes_buf[32];

    formatHBrickBuildBytes(bytes_buf, sizeof(bytes_buf), storage.brick_bytes);
    ImGui::TextWrapped(
        "Flat BRICK: %s (L0 cell closures, boundary attachments, port index, port CSR, seams)",
        bytes_buf
    );

    formatHBrickBuildBytes(bytes_buf, sizeof(bytes_buf), storage.hbrick_extra_bytes);
    ImGui::TextWrapped(
        "H-BRICK extra: %s (super-tile interface/boundary summaries and embeddings)",
        bytes_buf
    );

    formatHBrickBuildBytes(bytes_buf, sizeof(bytes_buf), storage.totalBytes());
    ImGui::TextWrapped("Total index: %s", bytes_buf);
}

void drawLevelGraphStats(const std::span<const HBrickLevelGraphStats> levels) {
    if (levels.empty()) {
        ImGui::TextDisabled("No graph statistics available.");
        return;
    }

    for (const HBrickLevelGraphStats& stats : levels) {
        if (stats.level == 0U) {
            ImGui::TextWrapped(
                "L0 flat port graph: 1 graph, %llu nodes",
                static_cast<unsigned long long>(stats.total_nodes)
            );
            continue;
        }

        ImGui::TextWrapped(
            "L%u super interface graphs: %u graphs, %llu total nodes, max %u nodes per graph",
            stats.level,
            stats.num_graphs,
            static_cast<unsigned long long>(stats.total_nodes),
            stats.max_nodes
        );
    }
}

}  // namespace

void beginBrickIndexBuild(
    BrickPanelState& brick,
    const MazeLayout& layout,
    const DirectedGridGraph& graph,
    const uint64_t graph_epoch
) {
    invalidateBrickIndex(brick);
    brick.graph_epoch_built = graph_epoch;
    brick.index_builder.begin(graph, layout, makeHBrickConfig(brick));
    brick.build_in_progress = brick.index_builder.running();
    if (!brick.build_in_progress) {
        brick.build_report = brick.index_builder.report();
        brick.index_status = brick.build_report.status;
        (void)brick.index_builder.takeIndex();
    }
}

void tickBrickIndexBuild(BrickPanelState& brick) noexcept {
    if (!brick.build_in_progress) {
        return;
    }

    const auto frame_start = std::chrono::steady_clock::now();
    constexpr auto kBatchableFrameBudget = std::chrono::milliseconds(12);

    do {
        const HBrickBuildStage stage = brick.index_builder.progress().stage;
        const bool batchable = stage == HBrickBuildStage::BaseTiles
            || stage == HBrickBuildStage::FinalizeBrick;

        if (brick.index_builder.step()) {
            break;
        }

        if (!batchable) {
            break;
        }

        const auto elapsed = std::chrono::steady_clock::now() - frame_start;
        if (elapsed >= kBatchableFrameBudget) {
            break;
        }
    } while (brick.index_builder.running());

    if (brick.index_builder.running()) {
        return;
    }

    brick.build_in_progress = false;
    brick.build_report = brick.index_builder.report();
    brick.index_status = brick.build_report.status;

    if (brick.build_report.status == BaselineStatus::Completed) {
        brick.cached_hbrick_index = brick.index_builder.takeIndex();
        brick.index_valid = true;
        applyBuildReportCounts(brick);
        return;
    }

    (void)brick.index_builder.takeIndex();
}

void beginBrickPanelFrame(BrickPanelState& brick) noexcept {
    if (brick.panel_requested) {
        brick.panel_open = true;
        brick.panel_requested = false;
    }
}

void drawBrickPanel(MapPanel& panel) {
    BrickPanelState& brick = panel.brick;
    OrientationState& orient = panel.orient;

    const std::string window_name =
        "BRICK: " + panel.set_name + "/" + panel.map_name + "###brick_" + std::to_string(panel.id);

    ImGui::SetNextWindowSize(ImVec2(380.0F, 680.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(window_name.c_str(), &brick.panel_open)) {
        ImGui::End();
        return;
    }

    ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);

    if (!orient.generated) {
        ImGui::TextDisabled("Generate a directed graph first (Orientation panel).");
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }

    if (!brickIndexMatchesEpoch(brick, orient.graph_epoch)) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.75F, 0.2F, 1.0F),
            "Index stale or not built — rebuild after graph changes."
        );
    }

    ImGui::SeparatorText("Base tile parameters");

    int tile_w = static_cast<int>(brick.base_tile_width);
    int tile_h = static_cast<int>(brick.base_tile_height);
    ImGui::InputInt("Tile width", &tile_w, 1, 2);
    ImGui::InputInt("Tile height", &tile_h, 1, 2);
    brick.base_tile_width = static_cast<uint32_t>(std::max(2, tile_w));
    brick.base_tile_height = static_cast<uint32_t>(std::max(2, tile_h));

    ImGui::SeparatorText("H-BRICK parameters (benchmark sync)");

    int group_w = static_cast<int>(brick.group_w);
    int group_h = static_cast<int>(brick.group_h);
    ImGui::InputInt("Group width", &group_w, 1, 1);
    ImGui::InputInt("Group height", &group_h, 1, 1);
    brick.group_w = static_cast<uint32_t>(std::max(1, group_w));
    brick.group_h = static_cast<uint32_t>(std::max(1, group_h));

    int max_depth = static_cast<int>(brick.max_depth);
    if (brick.max_depth == kHBrickFullDepth) {
        max_depth = 0;
    }
    ImGui::InputInt("Max depth (0 = full)", &max_depth, 1, 1);
    if (max_depth <= 0) {
        brick.max_depth = kHBrickFullDepth;
    } else {
        brick.max_depth = static_cast<uint32_t>(max_depth);
    }

    ImGui::InputFloat("Benchmark memory cap (GiB)", &brick.memory_gib, 1.0F, 8.0F, "%.0f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Used only when syncing to the reachability benchmark config. "
            "Panel builds always run with unlimited closure memory."
        );
    }

    ImGui::SeparatorText("Build");

    const bool build_ui_locked = brick.build_in_progress;
    if (build_ui_locked) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Build / rebuild index") && !build_ui_locked) {
        beginBrickIndexBuild(brick, panel.layout, orient.graph, orient.graph_epoch);
    }
    if (build_ui_locked) {
        ImGui::EndDisabled();
    }

    if (brick.build_in_progress) {
        const HBrickBuildProgress& progress = brick.index_builder.progress();
        const float fraction = progress.work_total > 0U
            ? static_cast<float>(progress.work_completed)
                / static_cast<float>(progress.work_total)
            : 0.0F;
        ImGui::ProgressBar(
            std::clamp(fraction, 0.0F, 1.0F),
            ImVec2(-1.0F, 0.0F),
            hbrickBuildStageLabel(progress.stage)
        );
        char detail[96];
        formatHBrickBuildProgressDetail(detail, sizeof(detail), progress);
        ImGui::TextDisabled("%s", detail);
    }

    if (ImGui::Button("Sync to benchmark config")) {
        orient.benchmark_config.brick_tile_size =
            TileSize{brick.base_tile_width, brick.base_tile_height};
        orient.benchmark_config.hbrick_group_size =
            GroupSize{brick.group_w, brick.group_h};
        orient.benchmark_config.hbrick_max_depth = brick.max_depth;
        orient.benchmark_config.max_memory_bytes = memoryBytesFromGib(brick.memory_gib);
    }

    if (brick.index_valid) {
        ImGui::TextWrapped(
            "Status: %s | tiles: %u | levels: %u | ports: %u | seams: %u",
            baselineStatusLabel(brick.index_status),
            brick.num_tiles,
            brick.num_hierarchy_levels,
            brick.num_ports,
            brick.num_seams
        );
        if (brick.cached_hbrick_index.has_value()) {
            ImGui::SeparatorText("Index storage");
            drawIndexStorageEstimate(brick.cached_hbrick_index->estimateStorageBreakdown());
            ImGui::SeparatorText("Graph nodes by level");
            drawLevelGraphStats(brick.cached_hbrick_index->levelGraphStats());
        }
    } else {
        ImGui::TextWrapped("Status: %s", baselineStatusLabel(brick.index_status));
    }

    if (brick.build_report.valid) {
        ImGui::SeparatorText("Last build report");

        char time_buf[32];
        formatHBrickBuildNanoseconds(
            time_buf,
            sizeof(time_buf),
            brick.build_report.total_nanoseconds
        );
        ImGui::Text("Status: %s", baselineStatusLabel(brick.build_report.status));
        if (!brick.build_report.status_detail.empty()) {
            ImGui::TextWrapped("%s", brick.build_report.status_detail.c_str());
        }
        ImGui::Text("Total time: %s", time_buf);

        char phase_buf[32];
        formatHBrickBuildNanoseconds(
            phase_buf,
            sizeof(phase_buf),
            brick.build_report.base_tile_nanoseconds
        );
        ImGui::TextWrapped(
            "Base tiles: %u completed (%u with L0 closure, %u impassable-only), "
            "%u skipped | phase %s",
            brick.build_report.num_base_completed,
            brick.build_report.num_base_with_closure,
            brick.build_report.num_base_empty_impassable,
            brick.build_report.num_base_skipped,
            phase_buf
        );

        formatHBrickBuildNanoseconds(
            phase_buf,
            sizeof(phase_buf),
            brick.build_report.base_tile_closure_nanoseconds
        );
        ImGui::TextWrapped(
            "L0 cell closure (adjacency + Warshall): %s across %u tiles",
            phase_buf,
            brick.build_report.num_base_with_closure
        );

        formatHBrickBuildNanoseconds(
            phase_buf,
            sizeof(phase_buf),
            brick.build_report.brick_finalize_nanoseconds
        );
        ImGui::Text("Port graph: %s", phase_buf);

        formatHBrickBuildNanoseconds(
            phase_buf,
            sizeof(phase_buf),
            brick.build_report.hierarchy_nanoseconds
        );
        ImGui::Text("Hierarchy: %u levels | %s", brick.build_report.num_hierarchy_levels, phase_buf);

        formatHBrickBuildNanoseconds(
            phase_buf,
            sizeof(phase_buf),
            brick.build_report.super_compose_nanoseconds
        );
        ImGui::Text("Super tiles: %s", phase_buf);

        for (const HBrickSuperLevelReport& level_report : brick.build_report.super_levels) {
            formatHBrickBuildNanoseconds(
                phase_buf,
                sizeof(phase_buf),
                level_report.compose_nanoseconds
            );
            ImGui::Text(
                "  L%u: %u regions (%u completed, %u skipped) | %s",
                level_report.level,
                level_report.num_regions,
                level_report.num_completed,
                level_report.num_skipped,
                phase_buf
            );
        }

        char bytes_buf[32];
        ImGui::TextWrapped(
            "Index topology: %u ports | %u seams",
            brick.build_report.num_ports,
            brick.build_report.num_seams
        );
        drawIndexStorageEstimate(HBrickStorageEstimate{
            brick.build_report.estimated_brick_storage_bytes,
            brick.build_report.estimated_hbrick_extra_storage_bytes,
        });
        if (!brick.build_report.level_graph_stats.empty()) {
            ImGui::SeparatorText("Graph nodes by level");
            drawLevelGraphStats(brick.build_report.level_graph_stats);
        }
    }

    ImGui::SeparatorText("Map overlays");

    const bool overlays_enabled = brick.index_valid;
    const bool hierarchy_enabled = overlays_enabled && brick.num_hierarchy_levels > 1U;

    if (!overlays_enabled) {
        ImGui::BeginDisabled();
    }

    if (!hierarchy_enabled) {
        ImGui::BeginDisabled();
    }
    ImGui::Checkbox("Hierarchy regions", &brick.overlay_hierarchy);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(
            "Super-region outlines from group width/height and max depth (levels >= 1)."
        );
    }
    if (hierarchy_enabled) {
        const int max_level = static_cast<int>(brick.num_hierarchy_levels) - 1;
        int level_pick = brick.hierarchy_display_level;
        if (level_pick > max_level) {
            level_pick = -1;
            brick.hierarchy_display_level = -1;
        }
        ImGui::SliderInt(
            "Hierarchy level",
            &level_pick,
            -1,
            max_level,
            level_pick < 0 ? "All" : (level_pick == 0 ? "L0 base" : "L%d")
        );
        brick.hierarchy_display_level = level_pick;
        if (level_pick < 0) {
            ImGui::TextDisabled(
                "All: L0 base tiles/ports/seams + L1..L%d super boundaries.",
                max_level
            );
        } else if (level_pick == 0) {
            ImGui::TextDisabled("L0 base tiles, ports, and seams only.");
        } else {
            ImGui::TextDisabled(
                "L%d: same overlays as All — tile boundaries, port cells, seam edges.",
                level_pick
            );
        }
    } else {
        ImGui::TextDisabled("No super levels for current group size / map / depth.");
    }
    if (!hierarchy_enabled) {
        ImGui::EndDisabled();
    }

    ImGui::Checkbox("Tile boundaries", &brick.overlay_tiles);
    ImGui::Checkbox("Port cells", &brick.overlay_ports);
    ImGui::Checkbox("Seam edges", &brick.overlay_seams);
    ImGui::Checkbox("Impassable-only tiles", &brick.show_impassable_only_tiles);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Draw tile outlines and (i,j) labels for tiles that contain no passable cells."
        );
    }
    if (!overlays_enabled) {
        ImGui::EndDisabled();
    }

    ImGui::TextDisabled(
        "Hierarchy from %.0fx (labels %.0fx); tiles from %.0fx (labels %.0fx); "
        "ports from %.0fx; seams from %.0fx.",
        kBrickHierarchyOverlayZoom,
        kBrickHierarchyLabelZoom,
        kBrickTileOverlayZoom,
        kBrickTileLabelZoom,
        kBrickPortOverlayZoom,
        kBrickSeamOverlayZoom
    );
    ImGui::TextDisabled(
        "Hover a map cell after building the index to see the L0 tile reachability summary."
    );

    ImGui::PopTextWrapPos();
    ImGui::End();
}

void drawBrickOverlays(
    ImDrawList* draw_list,
    const BrickPanelState& brick,
    const MazeLayout& layout,
    const float zoom,
    const ImVec2 image_min,
    const uint32_t x_first,
    const uint32_t x_last,
    const uint32_t y_first,
    const uint32_t y_last,
    const BrickOverlayPass pass
) {
    if (!brick.index_valid || !brick.cached_hbrick_index.has_value()) {
        return;
    }

    const HBrickIndex& hbrick_index = *brick.cached_hbrick_index;
    const BrickIndex& index = hbrick_index.brickIndex();
    const HierarchyTree& hierarchy = hbrick_index.hierarchy();

    if (pass == BrickOverlayPass::Hierarchy) {
        if (zoom < kBrickHierarchyOverlayZoom) {
            return;
        }
        if (brick.num_hierarchy_levels <= 1U) {
            return;
        }
        if (brick.hierarchy_display_level == 0) {
            return;
        }

        const bool draw_super_content = brick.overlay_tiles
            || brick.overlay_ports
            || brick.overlay_hierarchy
            || (brick.overlay_seams && brick.hierarchy_display_level >= 1);
        if (!draw_super_content) {
            return;
        }

        const bool show_labels = zoom >= kBrickHierarchyLabelZoom;

        uint32_t level_begin = 1U;
        uint32_t level_end = hierarchy.numLevels();
        if (brick.hierarchy_display_level >= 1) {
            level_begin = static_cast<uint32_t>(brick.hierarchy_display_level);
            level_end = level_begin + 1U;
            if (level_begin >= hierarchy.numLevels()) {
                return;
            }
        }

        for (uint32_t level = level_begin; level < level_end; ++level) {
            for (const RegionNode& region : hierarchy.level(level)) {
                const uint32_t region_max_x = region.origin.x + region.extent.width;
                const uint32_t region_max_y = region.origin.y + region.extent.height;
                if (region_max_x < x_first || region.origin.x >= x_last
                    || region_max_y < y_first || region.origin.y >= y_last) {
                    continue;
                }
                drawSuperRegionOverlays(
                    draw_list,
                    brick,
                    layout,
                    hierarchy,
                    region,
                    image_min,
                    zoom,
                    show_labels,
                    x_first,
                    x_last,
                    y_first,
                    y_last
                );
            }
        }

        if (brick.hierarchy_display_level >= 1 && brick.overlay_seams) {
            drawSuperLevelSeams(
                draw_list,
                brick,
                layout,
                hierarchy,
                index,
                static_cast<uint32_t>(brick.hierarchy_display_level),
                image_min,
                zoom,
                x_first,
                x_last,
                y_first,
                y_last
            );
        }
        return;
    }

    if (pass == BrickOverlayPass::TilesAndPorts) {
        if (brick.hierarchy_display_level >= 1) {
            return;
        }
        const bool show_tiles = brick.overlay_tiles && zoom >= kBrickTileOverlayZoom;
        const bool show_ports = brick.overlay_ports && zoom >= kBrickPortOverlayZoom;
        const bool show_labels = show_tiles && zoom >= kBrickTileLabelZoom;

        if (show_tiles) {
            for (const TileSlot& slot : index.tiles().decomposition().slots()) {
                if (slot.maxX() < x_first || slot.origin.x >= x_last
                    || slot.maxY() < y_first || slot.origin.y >= y_last) {
                    continue;
                }
                if (!shouldDrawTileOverlay(brick, layout, slot)) {
                    continue;
                }
                drawTileSlot(draw_list, slot, image_min, zoom, show_labels);
            }
        }

        if (show_ports) {
            for (const BaseTileSummary& summary : index.tiles().summaries()) {
                if (!shouldDrawTileOverlay(brick, layout, summary.slot)) {
                    continue;
                }
                for (const TilePort& port : summary.ports) {
                    if (port.coord.x < x_first || port.coord.x >= x_last
                        || port.coord.y < y_first || port.coord.y >= y_last) {
                        continue;
                    }
                    drawPortMarker(
                        draw_list,
                        cellCenter(image_min, zoom, port.coord),
                        port.side,
                        zoom
                    );
                }
            }
        }
        return;
    }

    if (!brick.overlay_seams || zoom < kBrickSeamOverlayZoom) {
        return;
    }

    const float head = std::clamp(zoom * 0.28F, 5.0F, 12.0F);
    const PortIndex& ports = index.ports();
    for (const SeamEdge& seam : index.seamEdges()) {
        const PortRecord& from = ports.port(seam.from_port_id);
        const PortRecord& to = ports.port(seam.to_port_id);
        if (from.coord.x < x_first && to.coord.x < x_first) {
            continue;
        }
        if (from.coord.x >= x_last && to.coord.x >= x_last) {
            continue;
        }
        if (from.coord.y < y_first && to.coord.y < y_first) {
            continue;
        }
        if (from.coord.y >= y_last && to.coord.y >= y_last) {
            continue;
        }
        drawSeamArrow(
            draw_list,
            cellCenter(image_min, zoom, from.coord),
            cellCenter(image_min, zoom, to.coord),
            zoom,
            head
        );
    }
}

[[nodiscard]] uint32_t localIndexForCoord(
    const BaseTileSummary& summary,
    const GridCoord coord
) noexcept {
    for (uint32_t index = 0U; index < summary.numLocalVertices(); ++index) {
        if (summary.local_coords[index].x == coord.x
            && summary.local_coords[index].y == coord.y) {
            return index;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

[[nodiscard]] uint32_t countMatrixRowBits(
    const BitMatrix& matrix,
    const uint32_t row
) noexcept {
    uint32_t count = 0U;
    for (uint32_t col = 0U; col < matrix.numCols(); ++col) {
        if (matrix.test(row, col)) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] uint32_t countMatrixColBits(
    const BitMatrix& matrix,
    const uint32_t col
) noexcept {
    uint32_t count = 0U;
    for (uint32_t row = 0U; row < matrix.numRows(); ++row) {
        if (matrix.test(row, col)) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] uint32_t countMatrixBits(const BitMatrix& matrix) noexcept {
    uint32_t count = 0U;
    for (uint32_t row = 0U; row < matrix.numRows(); ++row) {
        count += countMatrixRowBits(matrix, row);
    }
    return count;
}

bool drawBrickTileHoverInfo(
    const BrickPanelState& brick,
    const MazeLayout& layout,
    const GridCoord coord
) noexcept {
    if (!brick.index_valid || !brick.cached_hbrick_index.has_value()) {
        return false;
    }

    const HBrickIndex& hbrick_index = *brick.cached_hbrick_index;
    if (hbrick_index.status() != BaselineStatus::Completed) {
        return false;
    }

    const BrickIndex& index = hbrick_index.brickIndex();
    const TileDecomposition& decomposition = index.tiles().decomposition();
    if (!decomposition.contains(coord)) {
        return false;
    }

    TileSlotId slot_id{};
    if (!decomposition.slotAt(coord, slot_id)) {
        return false;
    }

    const BaseTileSummary& summary =
        index.tiles().summaryByIndex(slot_id.index);
    const uint32_t num_local = summary.numLocalVertices();
    const uint32_t num_ports = summary.numPorts();

    ImGui::TextWrapped(
        "Tile (%u,%u) slot %u  |  %u x %u cells",
        slot_id.tile_i,
        slot_id.tile_j,
        slot_id.index,
        summary.slot.extent.width,
        summary.slot.extent.height
    );

    if (num_local == 0U) {
        ImGui::TextWrapped("L0 reachability: none (impassable tile)");
        return true;
    }

    const uint32_t closure_rows = summary.local_closure.numRows();
    const uint32_t closure_cols = summary.local_closure.numCols();
    char bytes_buf[32];
    formatHBrickBuildBytes(
        bytes_buf,
        sizeof(bytes_buf),
        summary.local_closure.memoryBytes()
        + summary.boundary_summary.memoryBytes()
        + summary.vertex_to_boundary.memoryBytes()
        + summary.boundary_to_vertex.memoryBytes()
    );

    ImGui::TextWrapped(
        "L0 local closure: %u x %u (%u passable cells, %u closure bits, %s)",
        closure_rows,
        closure_cols,
        num_local,
        countMatrixBits(summary.local_closure),
        bytes_buf
    );
    ImGui::TextWrapped(
        "L0 boundary index S_T: %u x %u (%u ports)",
        summary.boundary_summary.numRows(),
        summary.boundary_summary.numCols(),
        num_ports
    );

    if (!layout.isPassable(coord)) {
        ImGui::TextWrapped("Hovered cell is impassable (no local index row/column).");
        return true;
    }

    const uint32_t local_index = localIndexForCoord(summary, coord);
    if (local_index == std::numeric_limits<uint32_t>::max()) {
        ImGui::TextWrapped("Hovered cell has no local closure index.");
        return true;
    }

    const uint32_t reachable_from =
        countMatrixRowBits(summary.local_closure, local_index);
    const uint32_t can_reach =
        countMatrixColBits(summary.local_closure, local_index);

    ImGui::TextWrapped(
        "Hovered cell local index %u: reaches %u / %u cells; reached from %u / %u cells",
        local_index,
        reachable_from,
        num_local,
        can_reach,
        num_local
    );

    return true;
}

}  // namespace hbrick::tools
