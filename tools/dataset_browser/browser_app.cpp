#include "browser_app.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include <imgui_internal.h>

#include "hbrick/io/movingai_loader.hpp"

namespace hbrick::tools {

namespace {

constexpr uint32_t kThumbnailEdge = 64U;
constexpr float kThumbnailDrawEdge = 52.0F;
constexpr float kMinZoom = 0.05F;
constexpr float kMaxZoom = 64.0F;
constexpr float kGridZoomThreshold = 8.0F;
constexpr float kLoupeZoom = 12.0F;
constexpr float kLoupeMinCanvasW = 340.0F;
constexpr float kLoupeMinCanvasH = 280.0F;

const char* policyLabel(const MovingAiPassabilityPolicy policy) {
    switch (policy) {
        case MovingAiPassabilityPolicy::GroundOnly:
            return "Ground only (. G)";
        case MovingAiPassabilityPolicy::GroundAndSwamp:
            return "Ground + swamp (. G S)";
        case MovingAiPassabilityPolicy::AllTraversable:
            return "All traversable (. G S W)";
        case MovingAiPassabilityPolicy::AnyTerrainLetter:
            return "Any terrain letter (weighted)";
        default:
            return "?";
    }
}

ImTextureID toImTexture(const GlTexture& texture) {
    return static_cast<ImTextureID>(static_cast<intptr_t>(texture.id));
}

bool containsCaseInsensitive(const std::string& haystack, const char* needle) {
    if (needle == nullptr || needle[0] == '\0') {
        return true;
    }
    const std::string_view pattern(needle);
    const auto it = std::search(
        haystack.begin(),
        haystack.end(),
        pattern.begin(),
        pattern.end(),
        [](const char a, const char b) {
            return std::tolower(static_cast<unsigned char>(a))
                == std::tolower(static_cast<unsigned char>(b));
        }
    );
    return it != haystack.end();
}

/** @brief Stable ImGui window name for a panel; title text varies, ID does not. */
std::string panelWindowName(const MapPanel& panel) {
    char buffer[160];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%s%s/%s###map_panel_%llu",
        panel.pinned ? "" : "~ ",
        panel.set_name.c_str(),
        panel.map_name.c_str(),
        static_cast<unsigned long long>(panel.id)
    );
    return buffer;
}

}  // namespace

BrowserApp::BrowserApp(std::filesystem::path dataset_root)
    : index_(scanDatasets(dataset_root)) {
    thumbnails_.resize(index_.sets.size());
    for (std::size_t i = 0; i < index_.sets.size(); ++i) {
        thumbnails_[i].resize(index_.sets[i].maps.size());
    }
    if (index_.sets.empty()) {
        status_ = "No datasets found under " + index_.root.string()
            + " - run datasets/movingai/fetch_movingai.sh --extract-only";
    } else {
        status_ = "Indexed " + std::to_string(index_.totalMaps()) + " maps in "
            + std::to_string(index_.sets.size())
            + " sets. Click opens a preview, double-click pins.";
    }
}

BrowserApp::~BrowserApp() {
    for (std::vector<GlTexture>& set : thumbnails_) {
        for (GlTexture& texture : set) {
            destroyTexture(texture);
        }
    }
    for (const std::unique_ptr<MapPanel>& panel : panels_) {
        destroyTexture(panel->texture);
    }
}

void BrowserApp::drawFrame() {
    hover_valid_ = false;

    drawMenuBar();
    drawStatusBar();

    const ImGuiID dockspace_id =
        ImGui::DockSpaceOverViewport(0U, ImGui::GetMainViewport());
    if (preset_pending_) {
        applyPreset(pending_preset_, dockspace_id);
        preset_pending_ = false;
    }

    handleShortcuts();

    if (show_gallery_) {
        drawGalleryPanel();
    }
    drawMapPanels();
    if (show_inspector_) {
        drawInspectorPanel();
    }

    redock_panels_ = false;
    pruneClosedPanels();
}

MapPanel* BrowserApp::activePanel() noexcept {
    for (const std::unique_ptr<MapPanel>& panel : panels_) {
        if (panel->id == active_panel_id_) {
            return panel.get();
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Chrome: menu bar, status bar, layout presets, shortcuts
// ---------------------------------------------------------------------------

void BrowserApp::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        MapPanel* active = activePanel();
        if (ImGui::MenuItem("Close active map", "Ctrl+W", false, active != nullptr)
            && active != nullptr) {
            active->open = false;
        }
        if (ImGui::MenuItem("Close all maps", nullptr, false, !panels_.empty())) {
            for (const std::unique_ptr<MapPanel>& panel : panels_) {
                panel->open = false;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", nullptr)) {
            quit_requested_ = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Layout: Browse")) {
            pending_preset_ = LayoutPreset::Browse;
            preset_pending_ = true;
        }
        if (ImGui::MenuItem("Layout: Focus")) {
            pending_preset_ = LayoutPreset::Focus;
            preset_pending_ = true;
        }
        if (ImGui::MenuItem("Layout: Compare")) {
            pending_preset_ = LayoutPreset::Compare;
            preset_pending_ = true;
        }
        ImGui::Separator();
        ImGui::MenuItem("Datasets panel", nullptr, &show_gallery_);
        ImGui::MenuItem("Inspector panel", nullptr, &show_inspector_);
        ImGui::Separator();
        ImGui::MenuItem("Loupe (picture-in-picture)", nullptr, &show_loupe_);
        ImGui::MenuItem("Grid lines when zoomed", nullptr, &show_grid_);
        ImGui::Separator();
        MapPanel* active = activePanel();
        if (ImGui::MenuItem("Fit active map", "F", false, active != nullptr)
            && active != nullptr) {
            active->fit_requested = true;
        }
        if (ImGui::MenuItem("Zoom 1:1", "1", false, active != nullptr)
            && active != nullptr) {
            active->zoom = 1.0F;
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void BrowserApp::drawStatusBar() {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar(
            "##hbrick_status_bar",
            ImGui::GetMainViewport(),
            ImGuiDir_Down,
            ImGui::GetFrameHeight(),
            flags)) {
        if (ImGui::BeginMenuBar()) {
            MapPanel* active = activePanel();
            // Hover data is from the previous frame's panel pass; at frame
            // rate that lag is invisible.
            if (active != nullptr && hover_valid_) {
                const GridCoord coord{hover_x_, hover_y_};
                const char cell = active->map.cellAt(coord);
                ImGui::Text(
                    "%s/%s  |  (%u, %u)  '%c'  %s  |  passable: %s  |  vertex %u  |  zoom %.2fx",
                    active->set_name.c_str(),
                    active->map_name.c_str(),
                    hover_x_,
                    hover_y_,
                    cell,
                    movingAiTerrainName(movingAiTerrainFromChar(cell)),
                    active->layout.isPassable(coord) ? "yes" : "no",
                    active->layout.vertexId(coord).value,
                    static_cast<double>(active->zoom)
                );
            } else if (active != nullptr) {
                ImGui::Text(
                    "%s/%s  |  %u x %u  |  %u passable  |  zoom %.2fx",
                    active->set_name.c_str(),
                    active->map_name.c_str(),
                    active->map.width(),
                    active->map.height(),
                    active->layout.passableCount(),
                    static_cast<double>(active->zoom)
                );
            } else {
                ImGui::TextUnformatted(status_.c_str());
            }
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

void BrowserApp::applyPreset(const LayoutPreset preset, const unsigned int dockspace_id) {
    ImGui::DockBuilderRemoveNodeChildNodes(dockspace_id);

    ImGuiID center = dockspace_id;
    show_gallery_ = preset != LayoutPreset::Focus;
    show_inspector_ = preset != LayoutPreset::Focus;

    switch (preset) {
        case LayoutPreset::Browse: {
            const ImGuiID left = ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Left, 0.24F, nullptr, &center
            );
            const ImGuiID right = ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Right, 0.28F, nullptr, &center
            );
            ImGui::DockBuilderDockWindow("Datasets", left);
            ImGui::DockBuilderDockWindow("Inspector", right);
            dock_main_ = center;
            dock_alt_ = center;
            break;
        }
        case LayoutPreset::Focus: {
            dock_main_ = center;
            dock_alt_ = center;
            break;
        }
        case LayoutPreset::Compare: {
            const ImGuiID left = ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Left, 0.22F, nullptr, &center
            );
            ImGui::DockBuilderDockWindow("Datasets", left);
            ImGui::DockBuilderDockWindow("Inspector", left);
            const ImGuiID right_half = ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Right, 0.50F, nullptr, &center
            );
            dock_main_ = center;
            dock_alt_ = right_half;
            break;
        }
    }

    ImGui::DockBuilderFinish(dockspace_id);
    redock_panels_ = true;
}

void BrowserApp::handleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }

    MapPanel* active = activePanel();
    if (active == nullptr) {
        return;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
        active->open = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        active->fit_requested = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_1, false)) {
        active->zoom = 1.0F;
    }
}

// ---------------------------------------------------------------------------
// Opening, closing, and rebuilding panels
// ---------------------------------------------------------------------------

void BrowserApp::openMap(
    const std::size_t set_index,
    const std::size_t map_index,
    const bool pinned
) {
    const SetEntry& set = index_.sets[set_index];
    const MapEntry& entry = set.maps[map_index];

    // Already open in some panel: focus it; double click promotes a preview.
    for (const std::unique_ptr<MapPanel>& panel : panels_) {
        if (panel->set_name == set.name && panel->map_name == entry.name) {
            if (pinned) {
                panel->pinned = true;
            }
            panel->focus_requested = true;
            active_panel_id_ = panel->id;
            return;
        }
    }

    MovingAiLoadResult result = loadMovingAiMap(entry.path);
    if (!result.ok()) {
        status_ = "Load failed: " + result.error;
        return;
    }

    MapPanel* target = nullptr;
    if (!pinned) {
        // VS Code preview behavior: reuse the existing preview panel.
        for (const std::unique_ptr<MapPanel>& panel : panels_) {
            if (!panel->pinned) {
                target = panel.get();
                break;
            }
        }
    }
    if (target == nullptr) {
        panels_.push_back(std::make_unique<MapPanel>());
        target = panels_.back().get();
        target->id = next_panel_id_++;
        target->pinned = pinned;
    }

    destroyTexture(target->texture);
    target->set_name = set.name;
    target->map_name = entry.name;
    target->map = std::move(result.map);
    target->layout = target->map.toMazeLayout(policy_);
    target->fit_requested = true;
    target->focus_requested = true;
    target->open = true;

    active_panel_id_ = target->id;
    rebuildPanelTexture(*target);

    status_ = set.name + "/" + entry.name + ": "
        + std::to_string(target->map.width()) + "x"
        + std::to_string(target->map.height()) + ", "
        + std::to_string(target->layout.passableCount()) + " passable cells";
}

void BrowserApp::closePanel(MapPanel& panel) {
    panel.open = false;
}

void BrowserApp::pruneClosedPanels() {
    bool active_closed = false;
    for (auto it = panels_.begin(); it != panels_.end();) {
        if ((*it)->open) {
            ++it;
            continue;
        }
        if ((*it)->id == active_panel_id_) {
            active_closed = true;
        }
        destroyTexture((*it)->texture);
        it = panels_.erase(it);
    }
    if (active_closed) {
        active_panel_id_ = panels_.empty() ? 0U : panels_.back()->id;
    }
}

void BrowserApp::rebuildPanelTexture(MapPanel& panel) {
    const std::vector<uint8_t> pixels = panel.view_mode == ViewMode::Terrain
        ? renderTerrainPixels(panel.map)
        : renderPassabilityPixels(panel.layout);
    uploadTexture(panel.texture, pixels, panel.map.width(), panel.map.height());
}

void BrowserApp::rebuildAllLayouts() {
    for (const std::unique_ptr<MapPanel>& panel : panels_) {
        panel->layout = panel->map.toMazeLayout(policy_);
        if (panel->view_mode == ViewMode::Passability) {
            rebuildPanelTexture(*panel);
        }
    }
}

GlTexture& BrowserApp::thumbnailFor(
    const std::size_t set_index,
    const std::size_t map_index
) {
    GlTexture& texture = thumbnails_[set_index][map_index];
    if (texture.valid()) {
        return texture;
    }

    const MovingAiLoadResult result =
        loadMovingAiMap(index_.sets[set_index].maps[map_index].path);
    if (!result.ok()) {
        return texture;
    }

    const std::vector<uint8_t> pixels = renderTerrainPixels(result.map);
    uint32_t thumb_width = 0;
    uint32_t thumb_height = 0;
    const std::vector<uint8_t> small = downsamplePixels(
        pixels,
        result.map.width(),
        result.map.height(),
        kThumbnailEdge,
        thumb_width,
        thumb_height
    );
    uploadTexture(texture, small, thumb_width, thumb_height);
    return texture;
}

// ---------------------------------------------------------------------------
// Gallery
// ---------------------------------------------------------------------------

void BrowserApp::drawGalleryPanel() {
    if (!ImGui::Begin("Datasets", &show_gallery_)) {
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##filter", "filter maps...", filter_, sizeof(filter_));

    for (std::size_t set_index = 0; set_index < index_.sets.size(); ++set_index) {
        const SetEntry& set = index_.sets[set_index];

        std::vector<std::size_t> visible;
        visible.reserve(set.maps.size());
        for (std::size_t i = 0; i < set.maps.size(); ++i) {
            if (containsCaseInsensitive(set.maps[i].name, filter_)) {
                visible.push_back(i);
            }
        }
        if (visible.empty()) {
            continue;
        }

        const std::string header = set.name + " ("
            + std::to_string(visible.size()) + "/"
            + std::to_string(set.maps.size()) + ")";
        if (!ImGui::CollapsingHeader(header.c_str())) {
            continue;
        }

        ImGui::PushID(static_cast<int>(set_index));
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()), kThumbnailDrawEdge + 6.0F);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const std::size_t map_index =
                    visible[static_cast<std::size_t>(row)];
                const MapEntry& entry = set.maps[map_index];
                ImGui::PushID(static_cast<int>(map_index));

                const GlTexture& thumb = thumbnailFor(set_index, map_index);
                if (thumb.valid()) {
                    const float longest = static_cast<float>(
                        thumb.width > thumb.height ? thumb.width : thumb.height
                    );
                    const float scale = kThumbnailDrawEdge / longest;
                    ImGui::Image(
                        toImTexture(thumb),
                        ImVec2(
                            static_cast<float>(thumb.width) * scale,
                            static_cast<float>(thumb.height) * scale
                        )
                    );
                } else {
                    ImGui::Dummy(ImVec2(kThumbnailDrawEdge, kThumbnailDrawEdge));
                }
                ImGui::SameLine();

                bool is_open = false;
                for (const std::unique_ptr<MapPanel>& panel : panels_) {
                    if (panel->set_name == set.name
                        && panel->map_name == entry.name) {
                        is_open = true;
                        break;
                    }
                }
                if (ImGui::Selectable(
                        entry.name.c_str(),
                        is_open,
                        ImGuiSelectableFlags_AllowDoubleClick,
                        ImVec2(0.0F, kThumbnailDrawEdge))) {
                    openMap(
                        set_index,
                        map_index,
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
                    );
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Map panels
// ---------------------------------------------------------------------------

void BrowserApp::drawMapPanels() {
    std::size_t panel_index = 0;
    for (const std::unique_ptr<MapPanel>& panel_ptr : panels_) {
        MapPanel& panel = *panel_ptr;

        if (redock_panels_) {
            const unsigned int target =
                (panel_index % 2U == 0U) ? dock_main_ : dock_alt_;
            ImGui::SetNextWindowDockID(target, ImGuiCond_Always);
        } else {
            ImGui::SetNextWindowDockID(dock_main_, ImGuiCond_FirstUseEver);
        }
        if (panel.focus_requested) {
            ImGui::SetNextWindowFocus();
            panel.focus_requested = false;
        }
        ImGui::SetNextWindowSize(ImVec2(760.0F, 560.0F), ImGuiCond_FirstUseEver);

        const std::string name = panelWindowName(panel);
        if (ImGui::Begin(name.c_str(), &panel.open)) {
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
                active_panel_id_ = panel.id;
            }
            drawPanelCanvas(panel);
        }
        ImGui::End();
        ++panel_index;
    }
}

void BrowserApp::drawPanelCanvas(MapPanel& panel) {
    if (ImGui::Button("Fit")) {
        panel.fit_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("1:1")) {
        panel.zoom = 1.0F;
    }
    if (!panel.pinned) {
        ImGui::SameLine();
        if (ImGui::Button("Pin")) {
            panel.pinned = true;
        }
    }
    ImGui::SameLine();
    ImGui::Text(
        "%u x %u   zoom %.2fx   %s",
        panel.map.width(),
        panel.map.height(),
        static_cast<double>(panel.zoom),
        panel.pinned ? "" : "(preview)"
    );

    const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 16.0F || canvas_size.y < 16.0F) {
        return;
    }

    const float map_width = static_cast<float>(panel.map.width());
    const float map_height = static_cast<float>(panel.map.height());

    if (panel.fit_requested && map_width > 0.0F && map_height > 0.0F) {
        panel.zoom = std::clamp(
            std::min(canvas_size.x / map_width, canvas_size.y / map_height),
            kMinZoom,
            kMaxZoom
        );
        panel.pan_x = (canvas_size.x - map_width * panel.zoom) * 0.5F;
        panel.pan_y = (canvas_size.y - map_height * panel.zoom) * 0.5F;
        panel.fit_requested = false;
    }

    ImGui::InvisibleButton(
        "canvas",
        canvas_size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle
    );
    const bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();

    if (hovered) {
        active_panel_id_ = panel.id;
    }

    if (ImGui::IsItemActive()
        && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F)
            || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0F))) {
        panel.pan_x += io.MouseDelta.x;
        panel.pan_y += io.MouseDelta.y;
    }

    if (hovered && io.MouseWheel != 0.0F) {
        const float new_zoom = std::clamp(
            panel.zoom * std::pow(1.15F, io.MouseWheel),
            kMinZoom,
            kMaxZoom
        );
        const float mouse_x = io.MousePos.x - canvas_pos.x;
        const float mouse_y = io.MousePos.y - canvas_pos.y;
        const float world_x = (mouse_x - panel.pan_x) / panel.zoom;
        const float world_y = (mouse_y - panel.pan_y) / panel.zoom;
        panel.pan_x = mouse_x - world_x * new_zoom;
        panel.pan_y = mouse_y - world_y * new_zoom;
        panel.zoom = new_zoom;
    }

    const ImVec2 image_min(canvas_pos.x + panel.pan_x, canvas_pos.y + panel.pan_y);
    const ImVec2 image_max(
        image_min.x + map_width * panel.zoom,
        image_min.y + map_height * panel.zoom
    );

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 canvas_end(
        canvas_pos.x + canvas_size.x,
        canvas_pos.y + canvas_size.y
    );
    draw_list->PushClipRect(canvas_pos, canvas_end, true);
    draw_list->AddRectFilled(canvas_pos, canvas_end, IM_COL32(18, 18, 22, 255));
    if (panel.texture.valid()) {
        draw_list->AddImage(toImTexture(panel.texture), image_min, image_max);
    }

    // Grid lines once individual cells are big enough to separate.
    if (show_grid_ && panel.zoom >= kGridZoomThreshold) {
        const ImU32 grid_color = IM_COL32(0, 0, 0, 70);
        const float x_first = std::max(0.0F, (canvas_pos.x - image_min.x) / panel.zoom);
        const float x_last = std::min(map_width, (canvas_end.x - image_min.x) / panel.zoom);
        const float y_first = std::max(0.0F, (canvas_pos.y - image_min.y) / panel.zoom);
        const float y_last = std::min(map_height, (canvas_end.y - image_min.y) / panel.zoom);
        for (float cx = std::ceil(x_first); cx <= x_last; cx += 1.0F) {
            const float sx = image_min.x + cx * panel.zoom;
            draw_list->AddLine(
                ImVec2(sx, image_min.y + y_first * panel.zoom),
                ImVec2(sx, image_min.y + y_last * panel.zoom),
                grid_color
            );
        }
        for (float cy = std::ceil(y_first); cy <= y_last; cy += 1.0F) {
            const float sy = image_min.y + cy * panel.zoom;
            draw_list->AddLine(
                ImVec2(image_min.x + x_first * panel.zoom, sy),
                ImVec2(image_min.x + x_last * panel.zoom, sy),
                grid_color
            );
        }
    }

    // Eyedropper: resolve the cell under the cursor.
    bool panel_hover_valid = false;
    if (hovered) {
        const float cell_x = (io.MousePos.x - image_min.x) / panel.zoom;
        const float cell_y = (io.MousePos.y - image_min.y) / panel.zoom;
        if (cell_x >= 0.0F && cell_y >= 0.0F
            && cell_x < map_width && cell_y < map_height) {
            panel_hover_valid = true;
            hover_valid_ = true;
            hover_x_ = static_cast<uint32_t>(cell_x);
            hover_y_ = static_cast<uint32_t>(cell_y);

            if (panel.zoom >= 3.0F) {
                const ImVec2 cell_min(
                    image_min.x + static_cast<float>(hover_x_) * panel.zoom,
                    image_min.y + static_cast<float>(hover_y_) * panel.zoom
                );
                const ImVec2 cell_max(
                    cell_min.x + panel.zoom,
                    cell_min.y + panel.zoom
                );
                draw_list->AddRect(cell_min, cell_max,
                                   IM_COL32(255, 200, 0, 255), 0.0F, 0, 1.5F);
            }
        }
    }

    if (show_loupe_ && panel_hover_valid
        && canvas_size.x >= kLoupeMinCanvasW && canvas_size.y >= kLoupeMinCanvasH
        && panel.zoom < kLoupeZoom) {
        drawLoupe(draw_list, panel, canvas_pos, canvas_size, io.MousePos);
    }

    draw_list->PopClipRect();
}

void BrowserApp::drawLoupe(
    ImDrawList* draw_list,
    MapPanel& panel,
    const ImVec2 canvas_pos,
    const ImVec2 canvas_size,
    const ImVec2 mouse_pos
) const {
    if (!panel.texture.valid()) {
        return;
    }

    const float map_width = static_cast<float>(panel.map.width());
    const float map_height = static_cast<float>(panel.map.height());

    const float edge = std::clamp(
        std::min(canvas_size.x, canvas_size.y) * 0.32F,
        140.0F,
        240.0F
    );
    const float region_cells = edge / kLoupeZoom;

    // Center the loupe on the hovered cell, clamped so the sampled window
    // stays inside the map.
    const float half = region_cells * 0.5F;
    float center_x = static_cast<float>(hover_x_) + 0.5F;
    float center_y = static_cast<float>(hover_y_) + 0.5F;
    center_x = std::clamp(center_x, std::min(half, map_width * 0.5F),
                          std::max(map_width - half, map_width * 0.5F));
    center_y = std::clamp(center_y, std::min(half, map_height * 0.5F),
                          std::max(map_height - half, map_height * 0.5F));

    const ImVec2 uv0(
        std::max(0.0F, (center_x - half) / map_width),
        std::max(0.0F, (center_y - half) / map_height)
    );
    const ImVec2 uv1(
        std::min(1.0F, (center_x + half) / map_width),
        std::min(1.0F, (center_y + half) / map_height)
    );

    // Sticky corner with hysteresis: the loupe keeps its corner until the
    // cursor comes close enough to crowd it, and only then jumps to the
    // corner farthest from the cursor. This avoids flip-flopping every time
    // the mouse crosses the canvas midpoint.
    const float margin = 12.0F;
    const float guard = 56.0F;
    const auto cornerPos = [&](const bool right, const bool bottom) {
        return ImVec2(
            right ? canvas_pos.x + canvas_size.x - margin - edge
                  : canvas_pos.x + margin,
            bottom ? canvas_pos.y + canvas_size.y - margin - edge
                   : canvas_pos.y + margin
        );
    };

    ImVec2 p0 = cornerPos(panel.loupe_right, panel.loupe_bottom);
    const bool cursor_crowds_loupe =
        mouse_pos.x >= p0.x - guard && mouse_pos.x <= p0.x + edge + guard
        && mouse_pos.y >= p0.y - guard && mouse_pos.y <= p0.y + edge + guard;
    if (cursor_crowds_loupe) {
        panel.loupe_right =
            mouse_pos.x < canvas_pos.x + canvas_size.x * 0.5F;
        panel.loupe_bottom =
            mouse_pos.y < canvas_pos.y + canvas_size.y * 0.5F;
        p0 = cornerPos(panel.loupe_right, panel.loupe_bottom);
    }
    const ImVec2 p1(p0.x + edge, p0.y + edge);

    draw_list->AddRectFilled(p0, p1, IM_COL32(10, 10, 12, 255));
    draw_list->AddImage(toImTexture(panel.texture), p0, p1, uv0, uv1);

    // Crosshair box on the hovered cell within the loupe.
    const float scale_x = edge / ((uv1.x - uv0.x) * map_width);
    const float scale_y = edge / ((uv1.y - uv0.y) * map_height);
    const ImVec2 cell_min(
        p0.x + (static_cast<float>(hover_x_) - uv0.x * map_width) * scale_x,
        p0.y + (static_cast<float>(hover_y_) - uv0.y * map_height) * scale_y
    );
    const ImVec2 cell_max(cell_min.x + scale_x, cell_min.y + scale_y);
    draw_list->AddRect(cell_min, cell_max, IM_COL32(255, 200, 0, 255), 0.0F, 0, 1.5F);

    draw_list->AddRect(p0, p1, IM_COL32(180, 180, 190, 220), 0.0F, 0, 1.5F);
}

// ---------------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------------

void BrowserApp::drawInspectorPanel() {
    if (!ImGui::Begin("Inspector", &show_inspector_)) {
        ImGui::End();
        return;
    }

    MapPanel* active = activePanel();

    ImGui::SeparatorText("View");

    int view = (active != nullptr && active->view_mode == ViewMode::Passability)
        ? 1 : 0;
    ImGui::BeginDisabled(active == nullptr);
    if (ImGui::RadioButton("Terrain colors", &view, 0) && active != nullptr
        && active->view_mode != ViewMode::Terrain) {
        active->view_mode = ViewMode::Terrain;
        rebuildPanelTexture(*active);
    }
    if (ImGui::RadioButton("MazeLayout passability", &view, 1) && active != nullptr
        && active->view_mode != ViewMode::Passability) {
        active->view_mode = ViewMode::Passability;
        rebuildPanelTexture(*active);
    }
    ImGui::EndDisabled();

    if (ImGui::BeginCombo("Policy", policyLabel(policy_))) {
        constexpr MovingAiPassabilityPolicy kPolicies[] = {
            MovingAiPassabilityPolicy::GroundOnly,
            MovingAiPassabilityPolicy::GroundAndSwamp,
            MovingAiPassabilityPolicy::AllTraversable,
            MovingAiPassabilityPolicy::AnyTerrainLetter,
        };
        for (const MovingAiPassabilityPolicy candidate : kPolicies) {
            if (ImGui::Selectable(policyLabel(candidate), candidate == policy_)
                && candidate != policy_) {
                policy_ = candidate;
                rebuildAllLayouts();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Active map");
    if (active != nullptr) {
        ImGui::Text("Set: %s", active->set_name.c_str());
        ImGui::Text("Map: %s%s",
                    active->map_name.c_str(),
                    active->pinned ? "" : "  (preview)");
        ImGui::Text("Type: %s", active->map.typeName().c_str());
        ImGui::Text("Size: %u x %u (%u cells)",
                    active->map.width(),
                    active->map.height(),
                    active->map.dimensions().numCells());
        ImGui::Text("Passable: %u cells", active->layout.passableCount());
        ImGui::Text("Open panels: %d", static_cast<int>(panels_.size()));
    } else {
        ImGui::TextDisabled("No map opened");
    }

    ImGui::SeparatorText("Cell under cursor");
    if (active != nullptr && hover_valid_) {
        const GridCoord coord{hover_x_, hover_y_};
        const char cell = active->map.cellAt(coord);
        const MovingAiTerrain terrain = active->map.terrainAt(coord);
        const bool passable = active->layout.isPassable(coord);
        const VertexId vertex = active->layout.vertexId(coord);

        uint8_t rgba[4] = {0U, 0U, 0U, 255U};
        terrainColor(cell, rgba);
        const ImVec4 swatch(
            static_cast<float>(rgba[0]) / 255.0F,
            static_cast<float>(rgba[1]) / 255.0F,
            static_cast<float>(rgba[2]) / 255.0F,
            1.0F
        );
        ImGui::ColorButton(
            "##cell_color",
            swatch,
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker
        );
        ImGui::SameLine();
        ImGui::Text("(%u, %u)", hover_x_, hover_y_);

        ImGui::Text("Raw char: '%c'", cell);
        ImGui::Text("Terrain: %s", movingAiTerrainName(terrain));
        ImGui::Text("Passable (policy): %s", passable ? "yes" : "no");
        ImGui::Text("MazeLayout vertex: %u", vertex.value);
    } else {
        ImGui::TextDisabled("Hover a map canvas");
    }

    ImGui::SeparatorText("About");
    ImGui::TextWrapped(
        "Read-only browser for the MovingAI 2D pathfinding benchmarks "
        "(Sturtevant 2012, ODC-BY). Maps are loaded through hbrick_io into "
        "MovingAiMap and converted to hbrick MazeLayout. Click a map for a "
        "preview panel, double-click to pin."
    );

    ImGui::End();
}

}  // namespace hbrick::tools
