#include "browser_app.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

#include <imgui_internal.h>

#include "hbrick/bench/bench_timer.hpp"
#include "hbrick/core/status_reporting.hpp"
#include "hbrick/io/movingai_loader.hpp"

#ifndef HBRICK_RECIPES_DIR
#define HBRICK_RECIPES_DIR "recipes"
#endif

namespace hbrick::tools {

namespace {

constexpr uint32_t kThumbnailEdge = 64U;
constexpr float kThumbnailDrawEdge = 52.0F;
constexpr float kMinZoom = 0.05F;
constexpr float kMaxZoom = 64.0F;
constexpr float kGridZoomThreshold = 8.0F;
constexpr float kArrowZoomThreshold = 10.0F;
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

const char* modeLabel(const GridEdgeConversionMode mode) {
    switch (mode) {
        case GridEdgeConversionMode::RandomAsymmetric:
            return "Random asymmetric";
        case GridEdgeConversionMode::BidirectionalAll:
            return "Bidirectional (baseline)";
        case GridEdgeConversionMode::AcyclicEastSouth:
            return "Acyclic east/south (DAG)";
        case GridEdgeConversionMode::GradientFlow:
            return "Gradient flow";
        default:
            return "?";
    }
}

/** @brief Degree on grid graphs is at most 4, so a linear span scan is exact and cheap. */
bool hasArc(const DirectedGridGraph& graph, const uint32_t from, const uint32_t to) {
    for (const uint32_t next : graph.outNeighbors(from)) {
        if (next == to) {
            return true;
        }
    }
    return false;
}

/** @brief Hover tooltip for the last submitted widget. */
void itemTooltip(const char* text) {
    if (ImGui::IsItemHovered(
            ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", text);
    }
}

/** @brief True when held modifier keys match @p ctrl / @p shift / @p alt exactly. */
bool modsMatch(
    const ImGuiIO& io,
    const bool ctrl,
    const bool shift,
    const bool alt
) {
    return io.KeyCtrl == ctrl && io.KeyShift == shift && io.KeyAlt == alt;
}

/**
 * @brief Key press with an exact modifier chord (no extra modifiers).
 *
 * Without this, @c Ctrl+G also matches @c Ctrl+Shift+G because ImGui only
 * checks that Ctrl is held, not that Shift is absent.
 */
bool chordPressed(
    const ImGuiIO& io,
    const ImGuiKey key,
    const bool ctrl,
    const bool shift,
    const bool alt
) {
    return modsMatch(io, ctrl, shift, alt) && ImGui::IsKeyPressed(key, false);
}

/** @brief Unmodified key press (no Ctrl, Shift, or Alt). */
bool chordPressed(const ImGuiIO& io, const ImGuiKey key) {
    return chordPressed(io, key, false, false, false);
}

/** @brief Ctrl + key (Shift and Alt must be up). */
bool chordPressed(
    const ImGuiIO& io,
    const ImGuiKey key,
    const bool ctrl
) {
    return chordPressed(io, key, ctrl, false, false);
}

/** @brief Ctrl+Shift + key (Alt must be up). */
bool chordPressed(
    const ImGuiIO& io,
    const ImGuiKey key,
    const bool ctrl,
    const bool shift
) {
    return chordPressed(io, key, ctrl, shift, false);
}

/**
 * @brief @c Ctrl+plus style chord; Shift may be held because @c + shares the
 *        @c = key on US layouts.
 */
bool ctrlPlusPressed(const ImGuiIO& io) {
    if (!io.KeyCtrl || io.KeyAlt) {
        return false;
    }
    return ImGui::IsKeyPressed(ImGuiKey_Equal, false)
        || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false);
}

/** @brief @c Ctrl+minus style chord with no other modifiers besides Ctrl. */
bool ctrlMinusPressed(const ImGuiIO& io) {
    return chordPressed(io, ImGuiKey_Minus, true, false, false)
        || chordPressed(io, ImGuiKey_KeypadSubtract, true, false, false);
}

/**
 * @brief One-shot menu row that does not collapse the parent menu.
 *
 * ImGui closes menus on item activation by default; we disable that so
 * toggles and layout picks can be applied repeatedly until the user clicks
 * outside the menu bar.
 */
bool menuAction(
    const char* label,
    const char* shortcut = nullptr,
    const bool enabled = true
) {
    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    const bool clicked = ImGui::MenuItem(label, shortcut, false, enabled);
    ImGui::PopItemFlag();
    return clicked;
}

/** @brief Checkbox menu row that keeps the parent menu open. */
void menuToggle(const char* label, const char* shortcut, bool* value) {
    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    ImGui::MenuItem(label, shortcut, value);
    ImGui::PopItemFlag();
}

uint64_t rollSeed() {
    std::random_device device;
    return (static_cast<uint64_t>(device()) << 32U) | device();
}

/** @brief Clears generated graph/SCC/probe data but keeps the chosen parameters. */
void resetOrientationData(OrientationState& orient) {
    clearProbe(orient);
    orient.generated = false;
    orient.graph = DirectedGridGraph{};
    orient.scc_valid = false;
    orient.num_components = 0;
    orient.largest_component = 0;
    orient.condensation_edges = 0;
    orient.component_of.clear();
    orient.component_size_of.clear();
    orient.component_sizes.clear();
    orient.component_order.clear();
    orient.density = {};
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

/** @brief Case-insensitive glob with `*` (any run) and `?` (any one char). */
bool wildcardMatch(const std::string_view text, const std::string_view pattern) {
    std::size_t t = 0;
    std::size_t p = 0;
    std::size_t star_p = std::string_view::npos;
    std::size_t star_t = 0;

    const auto lower = [](const char c) {
        return std::tolower(static_cast<unsigned char>(c));
    };

    while (t < text.size()) {
        if (p < pattern.size()
            && (pattern[p] == '?' || lower(pattern[p]) == lower(text[t]))) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star_p = p++;
            star_t = t;
        } else if (star_p != std::string_view::npos) {
            // Backtrack: let the last star swallow one more character.
            p = star_p + 1;
            t = ++star_t;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

/**
 * @brief Gallery filter: glob semantics when the user types wildcards,
 *        otherwise plain substring search.
 */
bool filterMatches(const std::string& name, const char* filter) {
    if (filter == nullptr || filter[0] == '\0') {
        return true;
    }
    const std::string_view pattern(filter);
    if (pattern.find('*') != std::string_view::npos
        || pattern.find('?') != std::string_view::npos) {
        return wildcardMatch(name, pattern);
    }
    return containsCaseInsensitive(name, filter);
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
    : index_(scanDatasets(dataset_root)),
      recipes_dir_(HBRICK_RECIPES_DIR) {
    thumbnails_.resize(index_.sets.size());
    for (std::size_t i = 0; i < index_.sets.size(); ++i) {
        thumbnails_[i].resize(index_.sets[i].maps.size());
    }
    refreshRecipeIndex();
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
        destroyOrientationTextures(panel->orient);
    }
}

void BrowserApp::prepareFrame() {
    if (ui_font_dirty_) {
        rebuildUiFont();
    }
}

void BrowserApp::rebuildUiFont() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig config;
    config.SizePixels = ui_font_size_;
    io.Fonts->AddFontDefault(&config);
    io.Fonts->Build();
    ui_font_dirty_ = false;
    font_gpu_refresh_needed_ = true;
}

void BrowserApp::adjustUiFont(const float delta_pixels) {
    ui_font_size_ = std::clamp(ui_font_size_ + delta_pixels, 12.0F, 28.0F);
    ui_font_dirty_ = true;
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
    if (show_shortcuts_) {
        drawShortcutsWindow();
    }

    drawDensityEstimateModals();
    drawBenchmarkModals();

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
        if (menuAction("Close active map", "Ctrl+W", active != nullptr)
            && active != nullptr) {
            active->open = false;
        }
        if (menuAction("Close all maps", nullptr, !panels_.empty())) {
            for (const std::unique_ptr<MapPanel>& panel : panels_) {
                panel->open = false;
            }
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Load recipe")) {
            ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
            const std::vector<std::filesystem::path> files = listRecipes(recipes_dir_);
            if (files.empty()) {
                ImGui::TextDisabled("No recipes in %s", recipes_dir_.string().c_str());
            }
            for (const std::filesystem::path& file : files) {
                if (menuAction(file.filename().string().c_str())) {
                    const std::optional<Recipe> recipe = loadRecipe(file);
                    if (recipe.has_value()) {
                        applyRecipe(*recipe);
                    } else {
                        status_ = "Could not parse recipe " + file.filename().string();
                    }
                }
            }
            ImGui::PopItemFlag();
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (menuAction("Quit", "Ctrl+Q")) {
            quit_requested_ = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (menuAction("Layout: Browse", "Ctrl+Shift+1")) {
            pending_preset_ = LayoutPreset::Browse;
            preset_pending_ = true;
        }
        if (menuAction("Layout: Focus", "Ctrl+Shift+2")) {
            pending_preset_ = LayoutPreset::Focus;
            preset_pending_ = true;
        }
        if (menuAction("Layout: Compare", "Ctrl+Shift+3")) {
            pending_preset_ = LayoutPreset::Compare;
            preset_pending_ = true;
        }
        ImGui::Separator();
        menuToggle("Datasets panel", "Ctrl+G", &show_gallery_);
        menuToggle("Inspector panel", "Ctrl+I", &show_inspector_);
        ImGui::Separator();
        menuToggle("Loupe (picture-in-picture)", "Ctrl+L", &show_loupe_);
        menuToggle("Grid lines when zoomed", "Ctrl+Shift+G", &show_grid_);
        ImGui::Separator();
        MapPanel* active = activePanel();
        if (menuAction("Fit active map", "F", active != nullptr) && active != nullptr) {
            active->fit_requested = true;
        }
        if (menuAction("Zoom 1:1", "1", active != nullptr) && active != nullptr) {
            active->zoom = 1.0F;
        }
        ImGui::Separator();
        if (menuAction("Font size larger", "Ctrl+=")) {
            adjustUiFont(1.0F);
        }
        if (menuAction("Font size smaller", "Ctrl+-")) {
            adjustUiFont(-1.0F);
        }
        if (menuAction("Font size reset", "Ctrl+0")) {
            ui_font_size_ = 16.0F;
            ui_font_dirty_ = true;
        }
        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
        if (ImGui::SliderFloat(
                "UI font size",
                &ui_font_size_,
                12.0F,
                28.0F,
                "%.0f px")) {
            ui_font_dirty_ = true;
        }
        ImGui::PopItemFlag();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        menuToggle("Keyboard shortcuts", "F1", &show_shortcuts_);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void BrowserApp::drawShortcutsWindow() {
    ImGui::SetNextWindowSize(ImVec2(520.0F, 520.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Keyboard shortcuts", &show_shortcuts_)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped(
        "Shortcuts are listed in the Help menu and work whenever a text field "
        "does not have keyboard focus."
    );
    ImGui::Separator();

    constexpr const char* kShortcuts[][2] = {
        {"F1", "Show or hide this shortcuts window"},
        {"Ctrl+Q", "Quit the application"},
        {"Ctrl+= / Ctrl+-", "Increase / decrease UI font size"},
        {"Ctrl+0", "Reset UI font size to 16 px"},
        {"Ctrl+Shift+1/2/3", "Browse / Focus / Compare layout preset"},
        {"Ctrl+G", "Toggle Datasets gallery panel"},
        {"Ctrl+I", "Toggle Inspector panel"},
        {"Ctrl+L", "Toggle picture-in-picture loupe"},
        {"Ctrl+Shift+G", "Toggle grid lines at high zoom"},
        {"Ctrl+Shift+R", "Toggle 'only maps with recipes' gallery filter"},
        {"F", "Fit the active map to its panel"},
        {"1", "Zoom the active map to 1:1"},
        {"[ / ]", "Zoom the active map out / in"},
        {"Ctrl+W", "Close the active map panel"},
        {"O", "Toggle orientation editor for the active map"},
        {"P", "Pin the active preview panel"},
        {"Alt+1/2/3", "Terrain / passability / SCC view on active map"},
        {"Esc / Space", "Clear the probe highlight on the active map"},
        {"Right-click", "Probe a cell (BFS or SCC, set in Orient panel)"},
        {"Ctrl+Shift+Enter", "Generate directed graph (orientation editor)"},
        {"D", "Roll dice / randomize seed (orientation editor)"},
        {"C", "Compute SCCs (orientation editor)"},
        {"Ctrl+S", "Save current recipe (orientation editor)"},
        {"Ctrl+Shift+P", "Switch probe mode: BFS reachability / SCC"},
        {"Mouse wheel", "Zoom around the cursor"},
        {"Left/middle drag", "Pan the canvas"},
        {"Click (gallery)", "Open map in the reusable preview panel"},
        {"Double-click (gallery)", "Open map in its own pinned panel"},
    };

    if (ImGui::BeginTable("##shortcuts", 2, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 170.0F);
        ImGui::TableSetupColumn("Action");
        for (const auto& row : kShortcuts) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row[0]);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", row[1]);
        }
        ImGui::EndTable();
    }

    ImGui::End();
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

    handleGlobalShortcuts();

    MapPanel* active = activePanel();
    if (active != nullptr) {
        handleMapShortcuts(*active);
    }
}

void BrowserApp::handleGlobalShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        show_shortcuts_ = !show_shortcuts_;
    }
    if (chordPressed(io, ImGuiKey_Q, true)) {
        quit_requested_ = true;
    }

    if (ctrlPlusPressed(io)) {
        adjustUiFont(1.0F);
    }
    if (ctrlMinusPressed(io)) {
        adjustUiFont(-1.0F);
    }
    if (chordPressed(io, ImGuiKey_0, true)) {
        ui_font_size_ = 16.0F;
        ui_font_dirty_ = true;
    }

    if (chordPressed(io, ImGuiKey_1, true, true)) {
        pending_preset_ = LayoutPreset::Browse;
        preset_pending_ = true;
    }
    if (chordPressed(io, ImGuiKey_2, true, true)) {
        pending_preset_ = LayoutPreset::Focus;
        preset_pending_ = true;
    }
    if (chordPressed(io, ImGuiKey_3, true, true)) {
        pending_preset_ = LayoutPreset::Compare;
        preset_pending_ = true;
    }

    if (chordPressed(io, ImGuiKey_G, true, true)) {
        show_grid_ = !show_grid_;
    } else if (chordPressed(io, ImGuiKey_G, true)) {
        show_gallery_ = !show_gallery_;
    }
    if (chordPressed(io, ImGuiKey_I, true)) {
        show_inspector_ = !show_inspector_;
    }
    if (chordPressed(io, ImGuiKey_L, true)) {
        show_loupe_ = !show_loupe_;
    }
    if (chordPressed(io, ImGuiKey_R, true, true)) {
        filter_recipes_only_ = !filter_recipes_only_;
    }
}

void BrowserApp::handleMapShortcuts(MapPanel& panel) {
    const ImGuiIO& io = ImGui::GetIO();

    if (chordPressed(io, ImGuiKey_W, true)) {
        panel.open = false;
        return;
    }
    if (chordPressed(io, ImGuiKey_F)) {
        panel.fit_requested = true;
    }
    if (chordPressed(io, ImGuiKey_1)) {
        panel.zoom = 1.0F;
    }
    if (chordPressed(io, ImGuiKey_LeftBracket)) {
        panel.zoom = std::clamp(panel.zoom / 1.25F, kMinZoom, kMaxZoom);
    }
    if (chordPressed(io, ImGuiKey_RightBracket)) {
        panel.zoom = std::clamp(panel.zoom * 1.25F, kMinZoom, kMaxZoom);
    }

    if ((chordPressed(io, ImGuiKey_Escape) || chordPressed(io, ImGuiKey_Space))
        && panel.orient.probe_valid) {
        clearProbe(panel.orient);
        status_ = "Probe cleared";
    }

    if (chordPressed(io, ImGuiKey_O)) {
        panel.orient.editor_open = !panel.orient.editor_open;
        if (panel.orient.editor_open) {
            panel.recipes_dirty = true;
        }
    }
    if (chordPressed(io, ImGuiKey_P) && !panel.pinned) {
        panel.pinned = true;
    }

    if (chordPressed(io, ImGuiKey_1, false, false, true)) {
        panel.view_mode = ViewMode::Terrain;
        rebuildPanelTexture(panel);
    }
    if (chordPressed(io, ImGuiKey_2, false, false, true)) {
        panel.view_mode = ViewMode::Passability;
        rebuildPanelTexture(panel);
    }
    if (chordPressed(io, ImGuiKey_3, false, false, true) && panel.orient.scc_valid) {
        panel.view_mode = ViewMode::Scc;
        rebuildPanelTexture(panel);
    }

    if (chordPressed(io, ImGuiKey_P, true, true)) {
        panel.orient.probe_mode = panel.orient.probe_mode == ProbeMode::Reachability
            ? ProbeMode::Component
            : ProbeMode::Reachability;
    }

    if (!panel.orient.editor_open) {
        return;
    }

    if (chordPressed(io, ImGuiKey_Enter, true, true)) {
        regenerateGraph(panel.orient, panel.layout);
        if (panel.view_mode == ViewMode::Scc) {
            panel.view_mode = ViewMode::Passability;
        }
        rebuildPanelTexture(panel);
    }
    if (chordPressed(io, ImGuiKey_D)) {
        panel.orient.seed = rollSeed();
        if (panel.orient.auto_generate) {
            regenerateGraph(panel.orient, panel.layout);
            if (panel.view_mode == ViewMode::Scc) {
                panel.view_mode = ViewMode::Passability;
            }
            rebuildPanelTexture(panel);
        }
    }
    if (chordPressed(io, ImGuiKey_C) && panel.orient.generated) {
        computeScc(panel.orient, panel.layout);
        panel.view_mode = ViewMode::Scc;
        rebuildPanelTexture(panel);
    }
    if (chordPressed(io, ImGuiKey_S, true) && panel.orient.generated) {
        savePanelRecipe(panel);
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
    // A reused preview panel keeps its editor and parameters but any graph or
    // analysis derived from the previous map is stale.
    resetOrientationData(target->orient);
    if (target->view_mode == ViewMode::Scc) {
        target->view_mode = ViewMode::Passability;
    }
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
        destroyOrientationTextures((*it)->orient);
        it = panels_.erase(it);
    }
    if (active_closed) {
        active_panel_id_ = panels_.empty() ? 0U : panels_.back()->id;
    }
}

void BrowserApp::rebuildPanelTexture(MapPanel& panel) {
    std::vector<uint8_t> pixels;
    switch (panel.view_mode) {
        case ViewMode::Terrain:
            pixels = renderTerrainPixels(panel.map);
            break;
        case ViewMode::Passability:
            pixels = renderPassabilityPixels(panel.layout);
            break;
        case ViewMode::Scc:
            pixels = panel.orient.scc_valid
                ? renderSccPixels(panel.layout, panel.orient)
                : renderPassabilityPixels(panel.layout);
            break;
    }
    uploadTexture(panel.texture, pixels, panel.map.width(), panel.map.height());
}

void BrowserApp::rebuildAllLayouts() {
    for (const std::unique_ptr<MapPanel>& panel : panels_) {
        panel->layout = panel->map.toMazeLayout(policy_);
        // The directed graph is derived from the layout, so it must follow
        // the policy change; SCC/probe results are recomputed on demand.
        if (panel->orient.generated) {
            regenerateGraph(panel->orient, panel->layout);
        }
        if (panel->view_mode == ViewMode::Scc) {
            panel->view_mode = ViewMode::Passability;
        }
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
    ImGui::InputTextWithHint(
        "##filter", "filter maps...  (* and ? wildcards)", filter_, sizeof(filter_)
    );
    itemTooltip(
        "Substring search by default.\n"
        "Use * (any run) and ? (any single character) for glob\n"
        "matching, e.g.  maze512-*-0  or  AR0?03SR"
    );
    ImGui::Checkbox("Only maps with recipes", &filter_recipes_only_);
    itemTooltip("Shortcut: Ctrl+Shift+R");

    for (std::size_t set_index = 0; set_index < index_.sets.size(); ++set_index) {
        const SetEntry& set = index_.sets[set_index];

        std::vector<std::size_t> visible;
        visible.reserve(set.maps.size());
        for (std::size_t i = 0; i < set.maps.size(); ++i) {
            if (!filterMatches(set.maps[i].name, filter_)) {
                continue;
            }
            if (filter_recipes_only_ && recipeCountFor(set_index, i) == 0U) {
                continue;
            }
            visible.push_back(i);
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

                // Recipe badge: orange dot in the thumbnail corner.
                const uint32_t recipes = recipeCountFor(set_index, map_index);
                if (recipes > 0U) {
                    const ImVec2 corner = ImGui::GetItemRectMin();
                    ImDrawList* overlay = ImGui::GetWindowDrawList();
                    const ImVec2 dot(corner.x + 7.0F, corner.y + 7.0F);
                    overlay->AddCircleFilled(dot, 5.0F, IM_COL32(20, 20, 24, 255));
                    overlay->AddCircleFilled(dot, 3.5F, IM_COL32(255, 170, 40, 255));
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "%u saved recipe%s", recipes, recipes == 1U ? "" : "s"
                        );
                    }
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
        panel.visible_now = ImGui::Begin(name.c_str(), &panel.open);
        if (panel.visible_now) {
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
                active_panel_id_ = panel.id;
            }
            drawPanelCanvas(panel);
        }
        ImGui::End();
        ++panel_index;
    }

    // Editor windows are drawn after all map panels so the canvas pass has
    // already updated hover and active-panel state for this frame. An editor
    // follows its map panel's visibility: it hides together with the panel's
    // tab and reappears when that tab is selected again, so edits always
    // target a map the user can see.
    for (const std::unique_ptr<MapPanel>& panel_ptr : panels_) {
        if (panel_ptr->orient.editor_open && panel_ptr->visible_now) {
            drawOrientationEditor(*panel_ptr);
        }
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
    if (ImGui::Button("Orient")) {
        panel.orient.editor_open = !panel.orient.editor_open;
        if (panel.orient.editor_open) {
            // Recipes may have changed on disk since the last time.
            panel.recipes_dirty = true;
        }
    }
    itemTooltip("Shortcut: O");
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
    if (panel.orient.probe_valid && panel.orient.probe_overlay.valid()) {
        draw_list->AddImage(
            toImTexture(panel.orient.probe_overlay), image_min, image_max
        );
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

    // Directed edge arrows once cells are large enough to read them.
    if (panel.orient.generated && panel.zoom >= kArrowZoomThreshold) {
        const float vx0 = std::max(0.0F, (canvas_pos.x - image_min.x) / panel.zoom);
        const float vx1 = std::min(map_width, (canvas_end.x - image_min.x) / panel.zoom);
        const float vy0 = std::max(0.0F, (canvas_pos.y - image_min.y) / panel.zoom);
        const float vy1 = std::min(map_height, (canvas_end.y - image_min.y) / panel.zoom);
        if (vx1 > vx0 && vy1 > vy0) {
            drawEdgeArrows(
                draw_list,
                panel,
                image_min,
                panel.zoom,
                panel.zoom,
                static_cast<uint32_t>(vx0),
                static_cast<uint32_t>(std::ceil(vx1)),
                static_cast<uint32_t>(vy0),
                static_cast<uint32_t>(std::ceil(vy1))
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

            // Right click probes reachability from the hovered cell.
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)
                && panel.orient.generated) {
                setProbe(panel, hover_x_, hover_y_);
            }

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

    // The loupe always magnifies enough for arrows, so show connectivity
    // whenever a directed graph exists.
    if (panel.orient.generated) {
        draw_list->PushClipRect(p0, p1, true);
        const ImVec2 origin(
            p0.x - uv0.x * map_width * scale_x,
            p0.y - uv0.y * map_height * scale_y
        );
        drawEdgeArrows(
            draw_list,
            panel,
            origin,
            scale_x,
            scale_y,
            static_cast<uint32_t>(uv0.x * map_width),
            static_cast<uint32_t>(std::ceil(uv1.x * map_width)),
            static_cast<uint32_t>(uv0.y * map_height),
            static_cast<uint32_t>(std::ceil(uv1.y * map_height))
        );
        draw_list->PopClipRect();
    }
    const ImVec2 cell_min(
        p0.x + (static_cast<float>(hover_x_) - uv0.x * map_width) * scale_x,
        p0.y + (static_cast<float>(hover_y_) - uv0.y * map_height) * scale_y
    );
    const ImVec2 cell_max(cell_min.x + scale_x, cell_min.y + scale_y);
    draw_list->AddRect(cell_min, cell_max, IM_COL32(255, 200, 0, 255), 0.0F, 0, 1.5F);

    draw_list->AddRect(p0, p1, IM_COL32(180, 180, 190, 220), 0.0F, 0, 1.5F);
}

// ---------------------------------------------------------------------------
// Directed orientation: arrows, probe, editor, recipes
// ---------------------------------------------------------------------------

void BrowserApp::drawEdgeArrows(
    ImDrawList* draw_list,
    const MapPanel& panel,
    const ImVec2 origin,
    const float scale_x,
    const float scale_y,
    const uint32_t x_first,
    const uint32_t x_last,
    const uint32_t y_first,
    const uint32_t y_last
) const {
    const DirectedGridGraph& graph = panel.orient.graph;
    const uint32_t width = panel.layout.width();
    const uint32_t height = panel.layout.height();
    const uint32_t x_end = std::min(x_last, width);
    const uint32_t y_end = std::min(y_last, height);
    if (x_first >= x_end || y_first >= y_end) {
        return;
    }

    // Safety valve: at the arrow zoom threshold this is never hit, but a
    // huge window could otherwise explode the draw list.
    const uint64_t cells = static_cast<uint64_t>(x_end - x_first)
        * static_cast<uint64_t>(y_end - y_first);
    if (cells > 60000ULL) {
        return;
    }

    const ImU32 color_bidirectional = IM_COL32(150, 150, 160, 150);
    const ImU32 color_one_way = IM_COL32(255, 150, 40, 235);
    const float head = 0.30F * std::min(scale_x, scale_y);

    const auto cellCenter = [&](const uint32_t x, const uint32_t y) {
        return ImVec2(
            origin.x + (static_cast<float>(x) + 0.5F) * scale_x,
            origin.y + (static_cast<float>(y) + 0.5F) * scale_y
        );
    };

    const auto drawAdjacency = [&](
        const uint32_t ux, const uint32_t uy,
        const uint32_t vx, const uint32_t vy
    ) {
        const uint32_t u = uy * width + ux;
        const uint32_t v = vy * width + vx;
        const bool forward = hasArc(graph, u, v);
        const bool backward = hasArc(graph, v, u);
        if (!forward && !backward) {
            return;
        }

        ImVec2 a = cellCenter(ux, uy);
        ImVec2 b = cellCenter(vx, vy);
        const float shrink = 0.20F;
        const ImVec2 sa(a.x + (b.x - a.x) * shrink, a.y + (b.y - a.y) * shrink);
        const ImVec2 sb(b.x - (b.x - a.x) * shrink, b.y - (b.y - a.y) * shrink);

        if (forward && backward) {
            draw_list->AddLine(sa, sb, color_bidirectional, 1.5F);
            return;
        }

        const ImVec2 tail = forward ? sa : sb;
        const ImVec2 tip = forward ? sb : sa;
        draw_list->AddLine(tail, tip, color_one_way, 1.5F);

        const float dx = tip.x - tail.x;
        const float dy = tip.y - tail.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 1.0F) {
            return;
        }
        const float nx = dx / length;
        const float ny = dy / length;
        const ImVec2 left(
            tip.x - nx * head - ny * head * 0.55F,
            tip.y - ny * head + nx * head * 0.55F
        );
        const ImVec2 right(
            tip.x - nx * head + ny * head * 0.55F,
            tip.y - ny * head - nx * head * 0.55F
        );
        draw_list->AddTriangleFilled(tip, left, right, color_one_way);
    };

    for (uint32_t y = y_first; y < y_end; ++y) {
        for (uint32_t x = x_first; x < x_end; ++x) {
            if (!panel.layout.isPassable(x, y)) {
                continue;
            }
            if (x + 1U < width && panel.layout.isPassable(x + 1U, y)) {
                drawAdjacency(x, y, x + 1U, y);
            }
            if (y + 1U < height && panel.layout.isPassable(x, y + 1U)) {
                drawAdjacency(x, y, x, y + 1U);
            }
        }
    }
}

void BrowserApp::setProbe(MapPanel& panel, const uint32_t x, const uint32_t y) {
    if (!panel.layout.isPassable(x, y)) {
        clearProbe(panel.orient);
        return;
    }
    const uint32_t vertex = panel.layout.vertexId(GridCoord{x, y}).value;

    if (panel.orient.probe_mode == ProbeMode::Component) {
        // The component probe needs SCC labels; compute them transparently.
        if (!panel.orient.scc_valid) {
            computeScc(panel.orient, panel.layout);
        }
        computeComponentProbe(panel.orient, vertex);
    } else {
        computeProbe(panel.orient, vertex);
    }
    if (!panel.orient.probe_valid) {
        return;
    }

    const std::vector<uint8_t> pixels =
        renderProbeOverlayPixels(panel.layout, panel.orient);
    uploadTexture(
        panel.orient.probe_overlay,
        pixels,
        panel.layout.width(),
        panel.layout.height()
    );

    const uint32_t passable = panel.layout.passableCount();
    const std::string cell = "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    if (panel.orient.probe_result_mode == ProbeMode::Component) {
        status_ = "SCC of " + cell + ": "
            + std::to_string(panel.orient.probe_reach_count) + " of "
            + std::to_string(passable) + " passable cells";
    } else {
        status_ = "Reachable from " + cell + ": "
            + std::to_string(panel.orient.probe_reach_count) + " of "
            + std::to_string(passable) + " passable cells, max depth "
            + std::to_string(panel.orient.probe_max_depth);
    }
}

void BrowserApp::setComponentProbe(MapPanel& panel, const uint32_t component) {
    if (!panel.orient.scc_valid) {
        return;
    }
    computeComponentProbeById(panel.orient, component);
    if (!panel.orient.probe_valid) {
        return;
    }

    const std::vector<uint8_t> pixels =
        renderProbeOverlayPixels(panel.layout, panel.orient);
    uploadTexture(
        panel.orient.probe_overlay,
        pixels,
        panel.layout.width(),
        panel.layout.height()
    );

    status_ = "Highlighted SCC " + std::to_string(component) + ": "
        + std::to_string(panel.orient.probe_reach_count) + " cells";
}

void BrowserApp::drawOrientationEditor(MapPanel& panel) {
    OrientationState& orient = panel.orient;

    char name[192];
    std::snprintf(
        name,
        sizeof(name),
        "Orientation: %s/%s###orient_%llu",
        panel.set_name.c_str(),
        panel.map_name.c_str(),
        static_cast<unsigned long long>(panel.id)
    );
    ImGui::SetNextWindowSize(ImVec2(380.0F, 640.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(name, &orient.editor_open)) {
        ImGui::End();
        return;
    }

    bool params_changed = false;

    ImGui::SeparatorText("Edge orientation");

    if (ImGui::BeginCombo("Mode", modeLabel(orient.mode))) {
        constexpr GridEdgeConversionMode kModes[] = {
            GridEdgeConversionMode::BidirectionalAll,
            GridEdgeConversionMode::AcyclicEastSouth,
            GridEdgeConversionMode::RandomAsymmetric,
            GridEdgeConversionMode::GradientFlow,
        };
        for (const GridEdgeConversionMode candidate : kModes) {
            if (ImGui::Selectable(modeLabel(candidate), candidate == orient.mode)
                && candidate != orient.mode) {
                orient.mode = candidate;
                params_changed = true;
            }
        }
        ImGui::EndCombo();
    }

    const bool uses_seed = orient.mode == GridEdgeConversionMode::RandomAsymmetric
        || orient.mode == GridEdgeConversionMode::GradientFlow;
    ImGui::BeginDisabled(!uses_seed);
    ImGui::SetNextItemWidth(180.0F);
    params_changed |= ImGui::InputScalar(
        "Seed", ImGuiDataType_U64, &orient.seed
    );
    itemTooltip(
        "Seed of the deterministic random generator.\n"
        "The same map + same parameters + same seed always\n"
        "produce the exact same directed graph."
    );
    ImGui::SameLine();
    if (ImGui::Button("Roll dice")) {
        orient.seed = rollSeed();
        params_changed = true;
    }
    itemTooltip("Shortcut: D");
    ImGui::EndDisabled();

    switch (orient.mode) {
        case GridEdgeConversionMode::RandomAsymmetric: {
            const bool one_way_edited = ImGui::SliderFloat(
                "p one-way", &orient.p_one_way, 0.0F, 1.0F, "%.3f"
            );
            itemTooltip(
                "Chance that a corridor between two cells becomes a single\n"
                "directed arc (direction chosen by coin flip).\n\n"
                "Together with 'p bidirectional' this partitions every\n"
                "corridor's fate, so the two sliders cannot exceed 1 in\n"
                "total: raising one pushes the other down."
            );
            const bool bidirectional_edited = ImGui::SliderFloat(
                "p bidirectional", &orient.p_bidirectional, 0.0F, 1.0F, "%.3f"
            );
            itemTooltip(
                "Chance that a corridor keeps both directions\n"
                "(a normal two-way passage)."
            );
            params_changed |= one_way_edited || bidirectional_edited;
            // The two probabilities partition each adjacency's outcome, so
            // their sum must stay within 1. The slider the user is dragging
            // wins; the other one yields.
            if (orient.p_one_way + orient.p_bidirectional > 1.0F) {
                if (one_way_edited) {
                    orient.p_bidirectional = 1.0F - orient.p_one_way;
                } else {
                    orient.p_one_way = 1.0F - orient.p_bidirectional;
                }
            }
            ImGui::TextWrapped(
                "Each corridor: %.0f%% two-way, %.0f%% one-way (random "
                "direction), %.0f%% removed entirely.",
                static_cast<double>(orient.p_bidirectional) * 100.0,
                static_cast<double>(orient.p_one_way) * 100.0,
                static_cast<double>(
                    1.0F - orient.p_one_way - orient.p_bidirectional
                ) * 100.0
            );
            break;
        }
        case GridEdgeConversionMode::GradientFlow: {
            params_changed |= ImGui::SliderFloat(
                "flow angle (deg)", &orient.gradient_angle_degrees,
                0.0F, 360.0F, "%.0f"
            );
            itemTooltip(
                "Global 'downhill' direction the arcs prefer.\n"
                "0 deg points east, 90 deg points south,\n"
                "180 deg west, 270 deg north."
            );
            params_changed |= ImGui::SliderFloat(
                "p bidirectional", &orient.p_bidirectional, 0.0F, 1.0F, "%.3f"
            );
            itemTooltip(
                "Chance that a corridor keeps both directions.\n"
                "Decided first, before any orientation happens."
            );
            params_changed |= ImGui::SliderFloat(
                "p against gradient", &orient.p_against_gradient,
                0.0F, 0.5F, "%.3f"
            );
            itemTooltip(
                "Of the corridors that became one-way, the chance the arc\n"
                "is flipped to point uphill. This backflow is what creates\n"
                "cycles (and therefore larger SCCs) in the flow field.\n\n"
                "This applies to a different stage than 'p bidirectional',\n"
                "so the two values are independent and never need to sum\n"
                "to 1."
            );
            const double p_bi = static_cast<double>(orient.p_bidirectional);
            const double p_against =
                static_cast<double>(orient.p_against_gradient);
            ImGui::TextWrapped(
                "Each corridor: %.0f%% two-way; the remaining %.0f%% become "
                "one-way arcs along the %.0f deg flow, of which %.0f%% are "
                "flipped backwards. No corridor is removed in this mode.",
                p_bi * 100.0,
                (1.0 - p_bi) * 100.0,
                static_cast<double>(orient.gradient_angle_degrees),
                p_against * 100.0
            );
            break;
        }
        case GridEdgeConversionMode::BidirectionalAll:
            ImGui::TextDisabled("No parameters for this mode");
            ImGui::TextWrapped(
                "Every corridor keeps both directions; the graph matches the "
                "undirected maze. Useful as a fully-connected baseline."
            );
            break;
        case GridEdgeConversionMode::AcyclicEastSouth:
            ImGui::TextDisabled("No parameters for this mode");
            ImGui::TextWrapped(
                "Every corridor is oriented east or south, producing a DAG: "
                "every passable cell becomes its own singleton SCC."
            );
            break;
    }

    ImGui::Checkbox("Regenerate on change", &orient.auto_generate);
    ImGui::SameLine();
    const bool generate_clicked = ImGui::Button("Generate");
    itemTooltip("Shortcut: Ctrl+Shift+Enter");

    if (generate_clicked || (params_changed && orient.auto_generate)) {
        regenerateGraph(orient, panel.layout);
        if (panel.view_mode == ViewMode::Scc) {
            // SCC labels died with the old graph; fall back until recomputed.
            panel.view_mode = ViewMode::Passability;
        }
        rebuildPanelTexture(panel);
    }

    if (orient.generated) {
        ImGui::TextWrapped(
            "Graph: %u cells passable, %llu directed edges",
            panel.layout.passableCount(),
            static_cast<unsigned long long>(orient.graph.numEdges())
        );
    } else {
        ImGui::TextDisabled("No graph generated yet");
    }

    ImGui::SeparatorText("SCC analysis");
    ImGui::BeginDisabled(!orient.generated);
    if (ImGui::Button("Compute SCCs")) {
        computeScc(orient, panel.layout);
        panel.view_mode = ViewMode::Scc;
        rebuildPanelTexture(panel);
    }
    itemTooltip(
        "Partitions the directed graph into strongly connected\n"
        "components (groups of cells that can all reach each other)\n"
        "and recolors the map so each component gets its own color.\n"
        "Shortcut: C"
    );
    if (orient.scc_valid) {
        const uint32_t passable = panel.layout.passableCount();
        ImGui::Text("Components: %u", orient.num_components);
        ImGui::TextWrapped(
            "Largest: %u cells (%.1f%% of passable)",
            orient.largest_component,
            passable > 0U
                ? 100.0 * static_cast<double>(orient.largest_component)
                    / static_cast<double>(passable)
                : 0.0
        );
        ImGui::TextWrapped(
            "Condensation: %u vertices, %llu arcs",
            orient.num_components,
            static_cast<unsigned long long>(orient.condensation_edges)
        );

        if (!orient.component_sizes.empty()) {
            // Top components by size; enough to read the distribution shape.
            const std::size_t bars =
                std::min<std::size_t>(orient.component_sizes.size(), 64U);
            float values[64];
            for (std::size_t i = 0; i < bars; ++i) {
                values[i] = static_cast<float>(orient.component_sizes[i]);
            }
            char overlay[64];
            std::snprintf(
                overlay, sizeof(overlay), "top %zu of %u SCC sizes",
                bars, orient.num_components
            );
            ImGui::PlotHistogram(
                "##scc_hist",
                values,
                static_cast<int>(bars),
                0,
                overlay,
                0.0F,
                static_cast<float>(orient.component_sizes.front()),
                ImVec2(-1.0F, 64.0F)
            );

            // Bins are clickable: hover names the component, click paints it
            // on the canvas via the probe overlay.
            if (ImGui::IsItemHovered()) {
                const ImVec2 rect_min = ImGui::GetItemRectMin();
                const ImVec2 rect_max = ImGui::GetItemRectMax();
                const float rect_width = std::max(1.0F, rect_max.x - rect_min.x);
                const float fraction =
                    (ImGui::GetIO().MousePos.x - rect_min.x) / rect_width;
                const std::size_t bin = std::min(
                    bars - 1U,
                    static_cast<std::size_t>(
                        std::max(0.0F, fraction) * static_cast<float>(bars)
                    )
                );
                ImGui::SetTooltip(
                    "SCC rank %zu: %u cells (id %u)\nClick to highlight it on the map",
                    bin + 1U,
                    orient.component_sizes[bin],
                    orient.component_order[bin]
                );
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    setComponentProbe(panel, orient.component_order[bin]);
                }
            }
        }
    }

    ImGui::BeginDisabled(orient.density_estimator.active());
    bool auto_stop_samples =
        orient.density_sample_mode == ReachabilityDensitySampleMode::AutoStopWhenStable;
    if (ImGui::Checkbox("Choose samples automatically", &auto_stop_samples)) {
        orient.density_sample_mode = auto_stop_samples
            ? ReachabilityDensitySampleMode::AutoStopWhenStable
            : ReachabilityDensitySampleMode::FixedSamples;
    }
    itemTooltip(
        "Stop once the density and its uncertainty (sigma) stabilize.\n"
        "The sample count below becomes a maximum cap."
    );
    ImGui::Checkbox("Parallel sampling", &orient.density_use_parallel);
    itemTooltip(
        "Run multiple BFS samples at once using distinct pre-shuffled\n"
        "start cells (one worker thread per sample)."
    );
    ImGui::SetNextItemWidth(120.0F);
    ImGui::Combo(
        "##density_samples",
        &orient.density_sample_index,
        "128\0"
        "256\0"
        "512\0"
        "1024\0"
        "2048\0"
        "4096\0"
        "8192\0"
        "16384\0"
    );
    itemTooltip(
        auto_stop_samples
            ? "Maximum random passable sources to BFS from.\n"
              "Small maps use every passable cell instead (exact)."
            : "Number of random passable sources to BFS from.\n"
              "Small maps use every passable cell instead (exact)."
    );
    ImGui::SameLine();
    if (ImGui::Button("Estimate reachable-pair density")) {
        beginDensityEstimateFromPanelSettings(orient, panel.layout);
        if (orient.density_estimator.active()) {
            orient.density_modal_requested = true;
        }
    }
    ImGui::EndDisabled();
    itemTooltip(
        "Fraction of ordered cell pairs (u, v) where v is reachable\n"
        "from u. 1.0 means fully connected, near 0 means shattered.\n"
        "Estimated by BFS from sampled sources; exact on small maps."
    );
    if (orient.density.valid) {
        if (orient.density.std_error == 0.0F) {
            ImGui::TextWrapped(
                "Density: %.4f (exact, all %u sources)",
                static_cast<double>(orient.density.density),
                orient.density.samples
            );
        } else {
            ImGui::TextWrapped(
                "Density: %.4f +/- %.4f (%u sampled sources)",
                static_cast<double>(orient.density.density),
                static_cast<double>(2.0F * orient.density.std_error),
                orient.density.samples
            );
        }
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Baseline benchmark");
    ImGui::BeginDisabled(
        !orient.generated
        || benchmarkWorkerRunning(orient)
        || (orient.benchmark_job != nullptr && orient.benchmark_job->active())
    );
    ImGui::SetNextItemWidth(120.0F);
    if (ImGui::Combo(
            "##benchmark_query_preset",
            &orient.benchmark_query_count_preset,
            "512\0"
            "4,096\0"
            "32,768\0"
            "262,144\0"
            "1,048,576\0"
            "4,194,304\0"
            "10,000,000\0"
            "Custom\0")) {
        static constexpr uint32_t kBenchmarkQueryPresets[] = {
            512U,
            4096U,
            32768U,
            262144U,
            1048576U,
            4194304U,
            10000000U,
        };
        if (orient.benchmark_query_count_preset >= 0
            && orient.benchmark_query_count_preset
                < static_cast<int>(std::size(kBenchmarkQueryPresets))) {
            orient.benchmark_timed_query_count =
                kBenchmarkQueryPresets[static_cast<std::size_t>(
                    orient.benchmark_query_count_preset
                )];
        }
    }
    itemTooltip("Quick presets for timed query pair count.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0F);
    ImGui::InputScalar(
        "##benchmark_timed_queries",
        ImGuiDataType_U32,
        &orient.benchmark_timed_query_count,
        nullptr,
        nullptr,
        "%u"
    );
    itemTooltip(
        "Number of timed reachability query pairs shared by every baseline.\n"
        "Large values use more memory and take longer; up to 4,294,967,295."
    );
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        orient.benchmark_query_count_preset = 7;
        if (orient.benchmark_timed_query_count == 0U) {
            orient.benchmark_timed_query_count = 1U;
        }
    }
    ImGui::SameLine();
    ImGui::Text("Timed query pairs");

    ImGui::SetNextItemWidth(120.0F);
    ImGui::InputScalar(
        "Warmup",
        ImGuiDataType_U32,
        &orient.benchmark_config.warmup_queries
    );
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0F);
    static constexpr uint32_t kBenchmarkCorrectnessCheckPresets[] = {
        128U,
        256U,
        512U,
        1024U,
        2048U,
    };
    if (orient.benchmark_correctness_check_preset >= 0
        && orient.benchmark_correctness_check_preset
            < static_cast<int>(std::size(kBenchmarkCorrectnessCheckPresets))) {
        orient.benchmark_config.correctness_check_count =
            kBenchmarkCorrectnessCheckPresets[static_cast<std::size_t>(
                orient.benchmark_correctness_check_preset
            )];
    }
    if (ImGui::Combo(
            "Checks",
            &orient.benchmark_correctness_check_preset,
            "128\0"
            "256\0"
            "512\0"
            "1,024\0"
            "2,048\0")) {
        if (orient.benchmark_correctness_check_preset >= 0
            && orient.benchmark_correctness_check_preset
                < static_cast<int>(std::size(kBenchmarkCorrectnessCheckPresets))) {
            orient.benchmark_config.correctness_check_count =
                kBenchmarkCorrectnessCheckPresets[static_cast<std::size_t>(
                    orient.benchmark_correctness_check_preset
                )];
        }
    }
    itemTooltip("Random pairs verified against BFS after timing completes.");

    ImGui::SetNextItemWidth(120.0F);
    ImGui::InputFloat("Memory cap (GiB)", &orient.benchmark_memory_gib, 1.0F, 8.0F, "%.0f");
    itemTooltip("Maximum index memory allowed during preprocessing.");

    ImGui::Checkbox(
        "Auto-stop slow closure preprocess",
        &orient.benchmark_closure_early_stop
    );
    itemTooltip(
        "When enabled, SccDagClosure and FullClosure stop Warshall preprocessing early\n"
        "if projected end-to-end speedup vs CsrBfs drops below 0.5×.\n"
        "When disabled, closure methods run all Warshall pivots unless you skip manually."
    );

    if (ImGui::Button("Run benchmark")) {
        const uint32_t warmup = orient.benchmark_config.warmup_queries;
        const uint32_t checks = orient.benchmark_config.correctness_check_count;
        const uint32_t timed_queries = std::max(1U, orient.benchmark_timed_query_count);
        const bool closure_early_stop = orient.benchmark_closure_early_stop;
        const float memory_gib = orient.benchmark_memory_gib;
        orient.benchmark_config = hbrick::ReachabilityBenchmarkConfig::allMethods();
        orient.benchmark_config.query_count = timed_queries;
        orient.benchmark_config.warmup_queries = warmup;
        orient.benchmark_config.correctness_check_count = checks;
        orient.benchmark_config.closure_enable_projected_speedup_early_stop =
            closure_early_stop;
        orient.benchmark_config.max_memory_bytes = static_cast<uint64_t>(
            static_cast<double>(memory_gib) * (1024.0 * 1024.0 * 1024.0)
        );
        beginReachabilityBenchmark(orient, panel.layout);
    }
    itemTooltip(
        "Compare reachability baselines on this directed graph using\n"
        "a shared random query workload. Results appear in the progress dialog."
    );
    ImGui::EndDisabled();

    ImGui::SeparatorText("Right-click probe");

    int probe_mode = orient.probe_mode == ProbeMode::Component ? 1 : 0;
    bool probe_mode_changed = false;
    if (ImGui::RadioButton("Reachable set (BFS)", &probe_mode, 0)) {
        probe_mode_changed = orient.probe_mode != ProbeMode::Reachability;
        orient.probe_mode = ProbeMode::Reachability;
    }
    itemTooltip(
        "Highlights every cell reachable from the clicked cell by\n"
        "following arcs forward. Colored by hop distance: yellow is\n"
        "close to the source, violet is the far frontier."
    );
    if (ImGui::RadioButton("Strongly connected component", &probe_mode, 1)) {
        probe_mode_changed = orient.probe_mode != ProbeMode::Component;
        orient.probe_mode = ProbeMode::Component;
    }
    itemTooltip(
        "Highlights the cells that can reach the clicked cell AND be\n"
        "reached from it (its SCC). Computes SCC labels automatically\n"
        "if needed."
    );

    if (probe_mode_changed && orient.probe_valid) {
        // Re-run the active probe under the newly selected mode.
        const GridCoord coord =
            panel.layout.coordFromVertex(VertexId{orient.probe_vertex});
        setProbe(panel, coord.x, coord.y);
    }

    if (orient.probe_valid) {
        const GridCoord coord =
            panel.layout.coordFromVertex(VertexId{orient.probe_vertex});
        const uint32_t passable = panel.layout.passableCount();
        const double percent = passable > 0U
            ? 100.0 * static_cast<double>(orient.probe_reach_count)
                / static_cast<double>(passable)
            : 0.0;
        if (orient.probe_result_mode == ProbeMode::Component) {
            ImGui::TextWrapped(
                "SCC of (%u, %u): %u of %u cells (%.1f%%)",
                coord.x, coord.y, orient.probe_reach_count, passable, percent
            );
        } else {
            ImGui::TextWrapped(
                "From (%u, %u): %u of %u cells (%.1f%%), max depth %u",
                coord.x, coord.y, orient.probe_reach_count, passable, percent,
                orient.probe_max_depth
            );
        }
        if (ImGui::Button("Clear probe")) {
            clearProbe(orient);
        }
        itemTooltip("Shortcut: Esc or Space");
    } else if (orient.generated) {
        ImGui::TextWrapped(
            "Right-click a passable cell on the canvas to probe it. "
            "Esc or Space clears the highlight."
        );
    } else {
        ImGui::TextDisabled("Generate a graph first");
    }

    ImGui::SeparatorText("Recipes");

    ImGui::BeginDisabled(!orient.generated);
    if (ImGui::Button("Save current as recipe")) {
        savePanelRecipe(panel);
    }
    ImGui::EndDisabled();
    itemTooltip("Shortcut: Ctrl+S");
    itemTooltip(
        "Stores the parameters only (never the maze) as a JSON file\n"
        "under recipes/. Changed parameters always produce a new file;\n"
        "saving identical parameters overwrites the same file."
    );

    // Saved recipes for this exact map, refreshed lazily.
    if (panel.recipes_dirty) {
        panel.map_recipes = listRecipesForMap(
            recipes_dir_, panel.set_name, panel.map_name
        );
        panel.recipes_dirty = false;
    }

    char popup_id[96];
    std::snprintf(
        popup_id,
        sizeof(popup_id),
        "Confirm delete recipe###recipe_delete_%llu",
        static_cast<unsigned long long>(panel.id)
    );

    if (panel.map_recipes.empty()) {
        ImGui::TextDisabled("No saved recipes for this map");
    } else {
        ImGui::TextWrapped(
            "%zu saved recipe(s) for this map:", panel.map_recipes.size()
        );
        constexpr float kRecipeButtonWidth = 64.0F;
        for (std::size_t i = 0; i < panel.map_recipes.size(); ++i) {
            const SavedRecipeEntry& entry = panel.map_recipes[i];
            const Recipe& saved = entry.recipe;
            ImGui::PushID(static_cast<int>(i));

            ImGui::BeginGroup();
            if (ImGui::Button("Load", ImVec2(kRecipeButtonWidth, 0.0F))) {
                applyRecipeToPanel(panel, saved);
            }
            if (ImGui::Button("Delete", ImVec2(kRecipeButtonWidth, 0.0F))) {
                panel.recipe_delete_pending_ = entry.path;
                panel.recipe_delete_modal_requested_ = true;
            }
            ImGui::EndGroup();

            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(entry.path.filename().string().c_str());
            if (entry.saved_at == "null") {
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                );
            } else {
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    ImVec4(0.55F, 0.60F, 0.68F, 1.0F)
                );
            }
            ImGui::SetWindowFontScale(0.82F);
            ImGui::TextUnformatted(entry.saved_at.c_str());
            ImGui::SetWindowFontScale(1.0F);
            ImGui::PopStyleColor();
            ImGui::TextWrapped(
                "%s, seed %llu, one-way %.2f, bi %.2f, angle %.0f, against %.2f",
                modeLabel(saved.mode),
                static_cast<unsigned long long>(saved.seed),
                static_cast<double>(saved.p_one_way),
                static_cast<double>(saved.p_bidirectional),
                static_cast<double>(saved.gradient_angle_degrees),
                static_cast<double>(saved.p_against_gradient)
            );
            ImGui::EndGroup();

            ImGui::PopID();
            ImGui::Spacing();
        }
    }

    // OpenPopup must run at the same ID stack depth as BeginPopupModal below.
    // Calling it from inside PushID(i) above would never match.
    if (panel.recipe_delete_modal_requested_) {
        ImGui::OpenPopup(popup_id);
        panel.recipe_delete_modal_requested_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0F, 0.0F), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(
            popup_id,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Are you sure you want to delete this recipe?");
        if (!panel.recipe_delete_pending_.empty()) {
            ImGui::TextUnformatted(
                panel.recipe_delete_pending_.filename().string().c_str()
            );
            const std::string saved_at = formatRecipeSavedAt(panel.recipe_delete_pending_);
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                saved_at == "null"
                    ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                    : ImVec4(0.55F, 0.60F, 0.68F, 1.0F)
            );
            ImGui::SetWindowFontScale(0.82F);
            ImGui::TextUnformatted(saved_at.c_str());
            ImGui::SetWindowFontScale(1.0F);
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120.0F, 0.0F))
            && !panel.recipe_delete_pending_.empty()) {
            deletePanelRecipe(panel, panel.recipe_delete_pending_);
            panel.recipe_delete_pending_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0F, 0.0F))) {
            panel.recipe_delete_pending_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

namespace {

const char* benchmarkStageLabel(
    const hbrick::ReachabilityBenchmarkProgress::Stage stage
) {
    using Stage = hbrick::ReachabilityBenchmarkProgress::Stage;
    switch (stage) {
        case Stage::Idle:
            return "Idle";
        case Stage::GeneratingPairs:
            return "Generating query pairs";
        case Stage::Preprocessing:
            return "Preprocessing";
        case Stage::WarmingUp:
            return "Warmup queries";
        case Stage::Querying:
            return "Timed queries";
        case Stage::CorrectnessCheck:
            return "Correctness checks";
        case Stage::Finished:
            return "Finished";
        case Stage::Cancelled:
            return "Cancelled";
    }
    return "Unknown";
}

void formatNanoseconds(char* buffer, const std::size_t size, const double ns) {
    if (ns >= 1.0e9) {
        std::snprintf(buffer, size, "%.2f s", ns / 1.0e9);
    } else if (ns >= 1.0e6) {
        std::snprintf(buffer, size, "%.2f ms", ns / 1.0e6);
    } else if (ns >= 1.0e3) {
        std::snprintf(buffer, size, "%.2f us", ns / 1.0e3);
    } else {
        std::snprintf(buffer, size, "%.0f ns", ns);
    }
}

void formatLargeCount(char* buffer, const std::size_t size, const uint64_t value) {
    if (value >= 1'000'000'000'000ULL) {
        std::snprintf(
            buffer,
            size,
            "%.2f T",
            static_cast<double>(value) / 1'000'000'000'000.0
        );
    } else if (value >= 1'000'000'000ULL) {
        std::snprintf(
            buffer,
            size,
            "%.2f B",
            static_cast<double>(value) / 1'000'000'000.0
        );
    } else if (value >= 1'000'000ULL) {
        std::snprintf(
            buffer,
            size,
            "%.2f M",
            static_cast<double>(value) / 1'000'000.0
        );
    } else if (value >= 10'000ULL) {
        std::snprintf(
            buffer,
            size,
            "%.2f K",
            static_cast<double>(value) / 1'000.0
        );
    } else {
        std::snprintf(buffer, size, "%llu", static_cast<unsigned long long>(value));
    }
}

void formatLargeCountPair(
    char* buffer,
    const std::size_t size,
    const uint64_t completed,
    const uint64_t total
) {
    char completed_label[24];
    char total_label[24];
    formatLargeCount(completed_label, sizeof(completed_label), completed);
    formatLargeCount(total_label, sizeof(total_label), total);
    std::snprintf(buffer, size, "%s / %s", completed_label, total_label);
}

void formatBytes(char* buffer, const std::size_t size, const uint64_t bytes) {
    if (bytes >= 1ULL << 40) {
        std::snprintf(
            buffer,
            size,
            "%.2f TiB",
            static_cast<double>(bytes) / static_cast<double>(1ULL << 40)
        );
    } else if (bytes >= 1ULL << 30) {
        std::snprintf(
            buffer,
            size,
            "%.2f GiB",
            static_cast<double>(bytes) / static_cast<double>(1ULL << 30)
        );
    } else if (bytes >= 1ULL << 20) {
        std::snprintf(
            buffer,
            size,
            "%.2f MiB",
            static_cast<double>(bytes) / static_cast<double>(1ULL << 20)
        );
    } else if (bytes >= 1ULL << 10) {
        std::snprintf(
            buffer,
            size,
            "%.2f KiB",
            static_cast<double>(bytes) / static_cast<double>(1ULL << 10)
        );
    } else {
        std::snprintf(buffer, size, "%llu B", static_cast<unsigned long long>(bytes));
    }
}

const char* activityDots() {
    static const char* kDots[] = {"", ".", "..", "..."};
    return kDots[static_cast<int>(ImGui::GetTime() * 2.5) % 4];
}

double benchmarkElapsedSeconds(const OrientationState& orient) {
    if (orient.benchmark_started_at.time_since_epoch().count() == 0) {
        return 0.0;
    }

    const std::chrono::steady_clock::time_point end_time =
        orient.benchmark_timer_frozen
            ? orient.benchmark_ended_at
            : std::chrono::steady_clock::now();
    const auto elapsed = end_time - orient.benchmark_started_at;
    return std::chrono::duration<double>(elapsed).count();
}

[[nodiscard]] bool benchmarkJobActive(const OrientationState& orient) noexcept {
    return orient.benchmark_job != nullptr && orient.benchmark_job->active();
}

[[nodiscard]] int benchmarkActiveMethodColumn(
    const hbrick::ReachabilityBenchmarkReport& report,
    const hbrick::ReachabilityBenchmarkProgress& progress,
    const bool running
) noexcept {
    if (!running || report.methods.empty()) {
        return -1;
    }

    using Stage = hbrick::ReachabilityBenchmarkProgress::Stage;
    switch (progress.stage) {
        case Stage::Preprocessing:
        case Stage::WarmingUp:
        case Stage::Querying:
        case Stage::CorrectnessCheck:
            for (std::size_t method_index = 0; method_index < report.methods.size();
                 ++method_index) {
                if (report.methods[method_index].method == progress.current_method) {
                    return static_cast<int>(method_index);
                }
            }
            return -1;
        case Stage::GeneratingPairs:
        case Stage::Idle:
        case Stage::Finished:
        case Stage::Cancelled:
            return -1;
    }

    return -1;
}

[[nodiscard]] bool benchmarkActiveColumnBlinkPhase() noexcept {
    return (static_cast<int>(ImGui::GetTime() * 1.8) & 1) == 0;
}

void highlightBenchmarkActiveColumn(const bool highlight) noexcept {
    if (!highlight) {
        return;
    }

    ImGui::TableSetBgColor(
        ImGuiTableBgTarget_CellBg,
        IM_COL32(70, 130, 210, 64)
    );
}

void drawBenchmarkActiveColumnText(
    const char* text,
    const bool highlight
) {
    if (highlight) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45F, 0.88F, 1.0F, 1.0F));
    }
    ImGui::TextUnformatted(text);
    if (highlight) {
        ImGui::PopStyleColor();
    }
}

[[nodiscard]] bool benchmarkMethodBuildsIndex(
    const hbrick::ReachabilityBaselineId method
) noexcept {
    switch (method) {
        case hbrick::ReachabilityBaselineId::CsrBfs:
        case hbrick::ReachabilityBaselineId::CsrDfs:
            return false;
        case hbrick::ReachabilityBaselineId::SccDagSearch:
        case hbrick::ReachabilityBaselineId::SccDagClosure:
        case hbrick::ReachabilityBaselineId::FullClosure:
        case hbrick::ReachabilityBaselineId::TwoHop:
        case hbrick::ReachabilityBaselineId::Grail:
            return true;
    }

    return true;
}

void formatBenchmarkPreprocessCell(
    char* buffer,
    const std::size_t size,
    const hbrick::BaselineBenchmarkMetrics& metrics,
    const hbrick::ReachabilityBenchmarkReport& report,
    const hbrick::ReachabilityBenchmarkProgress& progress,
    const std::size_t row_index,
    const bool running
) {
    using Stage = hbrick::ReachabilityBenchmarkProgress::Stage;
    const int active_column =
        benchmarkActiveMethodColumn(report, progress, running);
    if (running
        && progress.stage == Stage::Preprocessing
        && static_cast<int>(row_index) == active_column
        && metrics.preprocess_nanoseconds > 0U) {
        formatNanoseconds(
            buffer,
            size,
            static_cast<double>(metrics.preprocess_nanoseconds)
        );
        return;
    }

    if (running
        && progress.stage == Stage::Preprocessing
        && static_cast<int>(row_index) == active_column
        && progress.preprocess_started_ns > 0U) {
        const uint64_t now = hbrick::BenchTimer::steadyNowNanoseconds();
        formatNanoseconds(
            buffer,
            size,
            static_cast<double>(now - progress.preprocess_started_ns)
        );
        return;
    }

    if (!benchmarkMethodBuildsIndex(metrics.method)
        && metrics.status != hbrick::BaselineStatus::NotRun) {
        std::snprintf(buffer, size, "none");
        return;
    }

    if (metrics.preprocess_nanoseconds > 0U
        || metrics.status != hbrick::BaselineStatus::NotRun) {
        formatNanoseconds(
            buffer,
            size,
            static_cast<double>(metrics.preprocess_nanoseconds)
        );
        return;
    }

    std::snprintf(buffer, size, "-");
}

void drawBenchmarkMethodStatusCell(
    const hbrick::BaselineBenchmarkMetrics& metrics,
    const hbrick::ReachabilityBenchmarkReport& report,
    const hbrick::ReachabilityBenchmarkProgress& progress,
    const std::size_t row_index,
    const bool running
) {
    using Stage = hbrick::ReachabilityBenchmarkProgress::Stage;
    const int active_column =
        benchmarkActiveMethodColumn(report, progress, running);
    if (running
        && progress.stage == Stage::Preprocessing
        && static_cast<int>(row_index) == active_column) {
        ImGui::TextUnformatted("Running");
        return;
    }

    if (metrics.status != hbrick::BaselineStatus::NotRun) {
        ImGui::TextUnformatted(std::string(hbrick::toString(metrics.status)).c_str());
        return;
    }

    ImGui::TextUnformatted("-");
}

[[nodiscard]] uint64_t referenceBfsTotalNanoseconds(
    const hbrick::ReachabilityBenchmarkReport& report
) noexcept {
    if (report.reference_bfs_total_benchmark_nanoseconds > 0U) {
        return report.reference_bfs_total_benchmark_nanoseconds;
    }

    for (const hbrick::BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.method == hbrick::ReachabilityBaselineId::CsrBfs
            && metrics.total_benchmark_nanoseconds > 0U) {
            return metrics.total_benchmark_nanoseconds;
        }
    }

    return 0U;
}

[[nodiscard]] double referenceBfsMeanQueryNanoseconds(
    const hbrick::ReachabilityBenchmarkReport& report
) noexcept {
    if (report.reference_bfs_mean_query_nanoseconds > 0.0) {
        return report.reference_bfs_mean_query_nanoseconds;
    }

    for (const hbrick::BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.method == hbrick::ReachabilityBaselineId::CsrBfs
            && metrics.query_stats.count > 0U
            && metrics.query_stats.mean_nanoseconds > 0.0) {
            return metrics.query_stats.mean_nanoseconds;
        }
    }

    return 0.0;
}

[[nodiscard]] double liveQuerySpeedupVsBfs(
    const hbrick::ReachabilityBenchmarkReport& report,
    const hbrick::BaselineBenchmarkMetrics& metrics
) noexcept {
    if (metrics.speedup_vs_bfs > 0.0) {
        return metrics.speedup_vs_bfs;
    }

    if (metrics.query_stats.count == 0U
        || metrics.query_stats.mean_nanoseconds <= 0.0) {
        return 0.0;
    }

    if (metrics.method == hbrick::ReachabilityBaselineId::CsrBfs) {
        return 1.0;
    }

    const double reference_mean = referenceBfsMeanQueryNanoseconds(report);
    if (reference_mean <= 0.0) {
        return 0.0;
    }

    return reference_mean / metrics.query_stats.mean_nanoseconds;
}

[[nodiscard]] bool benchmarkClosurePreprocessActive(
    const hbrick::ReachabilityBenchmarkProgress& progress,
    const bool running
) noexcept {
    if (!running) {
        return false;
    }

    using Stage = hbrick::ReachabilityBenchmarkProgress::Stage;
    if (progress.stage != Stage::Preprocessing) {
        return false;
    }

    return progress.current_method == hbrick::ReachabilityBaselineId::SccDagClosure
        || progress.current_method == hbrick::ReachabilityBaselineId::FullClosure;
}

void drawBenchmarkResultsTable(
    const hbrick::ReachabilityBenchmarkReport& report,
    const hbrick::ReachabilityBenchmarkProgress& progress,
    const bool running,
    const char* table_id
) {
    if (report.methods.empty()) {
        return;
    }

    const int method_count = static_cast<int>(report.methods.size());
    const int column_count = method_count + 1;

    constexpr float kMinTableHeight = 220.0F;
    constexpr float kReservedBelowTable = 250.0F;
    const float table_height = std::max(
        kMinTableHeight,
        ImGui::GetContentRegionAvail().y - kReservedBelowTable
    );

    if (!ImGui::BeginTable(
            table_id,
            column_count,
            ImGuiTableFlags_Borders
                | ImGuiTableFlags_RowBg
                | ImGuiTableFlags_SizingStretchProp
                | ImGuiTableFlags_ScrollY,
            ImVec2(0.0F, table_height))) {
        return;
    }

    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 88.0F);
    for (int method_index = 0; method_index < method_count; ++method_index) {
        ImGui::TableSetupColumn(
            hbrick::reachabilityBaselineName(report.methods[static_cast<std::size_t>(method_index)].method),
            ImGuiTableColumnFlags_WidthStretch
        );
    }

    const int active_method_column =
        benchmarkActiveMethodColumn(report, progress, running);
    const bool active_blink =
        running
        && active_method_column >= 0
        && benchmarkActiveColumnBlinkPhase();
    const uint64_t reference_total = referenceBfsTotalNanoseconds(report);

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Metric");
    for (int method_index = 0; method_index < method_count; ++method_index) {
        ImGui::TableSetColumnIndex(method_index + 1);
        const bool column_active = running && method_index == active_method_column;
        if (column_active) {
            highlightBenchmarkActiveColumn(active_blink);
        }

        const char* method_name = hbrick::reachabilityBaselineName(
            report.methods[static_cast<std::size_t>(method_index)].method
        );
        if (column_active) {
            drawBenchmarkActiveColumnText(method_name, active_blink);
            itemTooltip("This baseline is running now.");
        } else {
            ImGui::TextUnformatted(method_name);
        }
    }

    const auto draw_metric_row = [&](
        const char* label,
        const char* tooltip,
        auto&& draw_cell
    ) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        if (tooltip != nullptr) {
            itemTooltip(tooltip);
        }

        for (int method_index = 0; method_index < method_count; ++method_index) {
            const std::size_t row_index = static_cast<std::size_t>(method_index);
            const hbrick::BaselineBenchmarkMetrics& metrics =
                report.methods[row_index];
            ImGui::TableSetColumnIndex(method_index + 1);
            const bool column_active = running && method_index == active_method_column;
            if (column_active) {
                highlightBenchmarkActiveColumn(active_blink);
                if (active_blink) {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        ImVec4(0.45F, 0.88F, 1.0F, 1.0F)
                    );
                }
            }
            draw_cell(metrics, row_index);
            if (column_active && active_blink) {
                ImGui::PopStyleColor();
            }
        }
    };

    draw_metric_row(
        "Status",
        "Preprocess and query outcome for this baseline.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t row_index) {
        drawBenchmarkMethodStatusCell(metrics, report, progress, row_index, running);
    });

    draw_metric_row(
        "Preprocess",
        "One-time index build time. Query-only baselines (CsrBfs, CsrDfs) show none.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t row_index) {
        char buffer[64];
        formatBenchmarkPreprocessCell(
            buffer,
            sizeof(buffer),
            metrics,
            report,
            progress,
            row_index,
            running
        );
        ImGui::TextUnformatted(buffer);
    });

    draw_metric_row(
        "Index",
        "Stored index bytes after preprocessing. TwoHop/Grail: measured label heap "
        "(TwoHop skips only when live label storage exceeds the cap during build). "
        "Closures: reachability bit matrix.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t row_index) {
        (void)row_index;
        char buffer[64];
        if (benchmarkMethodBuildsIndex(metrics.method)
            && metrics.estimated_index_bytes > 0U) {
            formatBytes(buffer, sizeof(buffer), metrics.estimated_index_bytes);
            ImGui::TextUnformatted(buffer);
            if (metrics.method == hbrick::ReachabilityBaselineId::TwoHop) {
                itemTooltip(
                    "All-vertex 2-hop hub labels. Memory is checked after each hub using "
                    "measured label storage; preprocessing stops only if that exceeds the cap."
                );
            }
        } else if (!benchmarkMethodBuildsIndex(metrics.method)
            && metrics.status != hbrick::BaselineStatus::NotRun) {
            ImGui::TextUnformatted("none");
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "DAG |V|",
        "SccDagClosure: condensation DAG nodes (SCC count C). "
        "FullClosure: original graph vertex count |V|. "
        "Warshall cost scales with C² or |V|².",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        char buffer[64];
        if (metrics.method == hbrick::ReachabilityBaselineId::SccDagClosure
            && metrics.warshall_matrix_order > 0U) {
            formatLargeCount(buffer, sizeof(buffer), metrics.warshall_matrix_order);
            ImGui::TextUnformatted(buffer);
        } else if (metrics.method == hbrick::ReachabilityBaselineId::FullClosure
            && metrics.warshall_matrix_order > 0U) {
            formatLargeCount(buffer, sizeof(buffer), metrics.warshall_matrix_order);
            ImGui::TextUnformatted(buffer);
        } else if (metrics.method == hbrick::ReachabilityBaselineId::FullClosure
            && report.num_vertices > 0U
            && metrics.status != hbrick::BaselineStatus::NotRun) {
            formatLargeCount(buffer, sizeof(buffer), report.num_vertices);
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "Warsh k",
        "Floyd-Warshall outer pivot index k completed (k / C or k / |V|). "
        "This is not matrix squaring: each pivot updates rows that reach pivot k.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t row_index) {
        (void)row_index;
        if ((metrics.method == hbrick::ReachabilityBaselineId::SccDagClosure
                || metrics.method == hbrick::ReachabilityBaselineId::FullClosure)
            && metrics.warshall_pivot_total > 0U) {
            char buffer[64];
            formatLargeCountPair(
                buffer,
                sizeof(buffer),
                metrics.warshall_pivots_completed,
                metrics.warshall_pivot_total
            );
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "RSS d",
        "Process resident-set increase during preprocessing (Linux only).",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        char buffer[64];
        if (metrics.preprocess_rss_delta_bytes > 0U) {
            formatBytes(buffer, sizeof(buffer), metrics.preprocess_rss_delta_bytes);
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "Mean q",
        "Mean latency of timed reachability queries.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        char buffer[64];
        if (metrics.query_stats.count > 0U) {
            formatNanoseconds(
                buffer,
                sizeof(buffer),
                metrics.query_stats.mean_nanoseconds
            );
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "P95 q",
        "95th percentile timed query latency.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        char buffer[64];
        if (metrics.query_stats.count > 0U) {
            formatNanoseconds(
                buffer,
                sizeof(buffer),
                static_cast<double>(metrics.query_stats.p95_nanoseconds)
            );
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "QPS",
        "Timed queries per second (overall throughput).",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        char buffer[64];
        if (metrics.query_stats.queries_per_second > 0.0) {
            formatLargeCount(
                buffer,
                sizeof(buffer),
                static_cast<uint64_t>(metrics.query_stats.queries_per_second)
            );
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "Reach %",
        "Fraction of timed queries answered reachable on the shared workload.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        char buffer[64];
        if (metrics.query_stats.count > 0U) {
            std::snprintf(
                buffer,
                sizeof(buffer),
                "%.1f",
                metrics.reachable_ratio * 100.0
            );
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "+ mean",
        "Mean timed query latency when the answer is reachable.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        char buffer[64];
        if (metrics.positive_query_count > 0U) {
            formatNanoseconds(
                buffer,
                sizeof(buffer),
                metrics.mean_positive_query_nanoseconds
            );
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "- mean",
        "Mean timed query latency when the answer is unreachable.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        char buffer[64];
        if (metrics.negative_query_count > 0U) {
            formatNanoseconds(
                buffer,
                sizeof(buffer),
                metrics.mean_negative_query_nanoseconds
            );
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "Tree hit",
        "GRAIL only: timed queries certified by tree interval labeling.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        if (metrics.method == hbrick::ReachabilityBaselineId::Grail
            && metrics.query_stats.count > 0U) {
            char buffer[64];
            formatLargeCount(buffer, sizeof(buffer), metrics.grail_tree_hits);
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "BFS fb",
        "GRAIL only: timed queries that fell back to BFS.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        if (metrics.method == hbrick::ReachabilityBaselineId::Grail
            && metrics.query_stats.count > 0U) {
            char buffer[64];
            formatLargeCount(buffer, sizeof(buffer), metrics.grail_bfs_fallbacks);
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "Total",
        "Preprocess + warmup + timed queries. Excludes correctness checks.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        char buffer[64];
        if (metrics.total_benchmark_nanoseconds > 0U
            || metrics.status != hbrick::BaselineStatus::NotRun) {
            formatNanoseconds(
                buffer,
                sizeof(buffer),
                static_cast<double>(metrics.total_benchmark_nanoseconds)
            );
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "Query x",
        "Mean timed-query speedup vs CsrBfs (>1 is faster). Updates live during querying.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        char buffer[64];
        const double query_speedup = liveQuerySpeedupVsBfs(report, metrics);
        if (query_speedup > 0.0) {
            std::snprintf(buffer, sizeof(buffer), "%.2fx", query_speedup);
            ImGui::TextUnformatted(buffer);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "Total x",
        "End-to-end speedup vs CsrBfs: preprocess + warmup + timed queries (>1 is faster). "
        "During closure preprocess, ~value is projected total speedup from the early-stop policy.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        char buffer[64];
        if (metrics.status == hbrick::BaselineStatus::SkippedByPolicy) {
            ImGui::TextUnformatted("-");
        } else if (metrics.total_speedup_vs_bfs > 0.0) {
            std::snprintf(buffer, sizeof(buffer), "%.2fx", metrics.total_speedup_vs_bfs);
            ImGui::TextUnformatted(buffer);
        } else if (running
            && (metrics.method == hbrick::ReachabilityBaselineId::FullClosure
                || metrics.method == hbrick::ReachabilityBaselineId::SccDagClosure)
            && metrics.projected_total_speedup_vs_bfs > 0.0) {
            if (metrics.projected_total_speedup_vs_bfs >= 0.01) {
                std::snprintf(
                    buffer,
                    sizeof(buffer),
                    "~%.2fx",
                    metrics.projected_total_speedup_vs_bfs
                );
            } else if (metrics.projected_total_speedup_vs_bfs >= 0.0001) {
                std::snprintf(
                    buffer,
                    sizeof(buffer),
                    "~%.4fx",
                    metrics.projected_total_speedup_vs_bfs
                );
            } else {
                std::snprintf(buffer, sizeof(buffer), "~<0.0001x");
            }
            ImGui::TextUnformatted(buffer);
        } else if (reference_total > 0U
            && metrics.total_benchmark_nanoseconds > 0U
            && metrics.method != hbrick::ReachabilityBaselineId::CsrBfs
            && metrics.status == hbrick::BaselineStatus::Completed) {
            const double ratio =
                static_cast<double>(reference_total)
                / static_cast<double>(metrics.total_benchmark_nanoseconds);
            std::snprintf(buffer, sizeof(buffer), "%.2fx", ratio);
            ImGui::TextUnformatted(buffer);
        } else if (metrics.method == hbrick::ReachabilityBaselineId::CsrBfs
            && metrics.total_benchmark_nanoseconds > 0U
            && metrics.status == hbrick::BaselineStatus::Completed) {
            ImGui::TextUnformatted("1.00x");
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    draw_metric_row(
        "Mismatch",
        "Correctness spot-checks that disagreed with BFS / total spot-checks run.",
        [&](const hbrick::BaselineBenchmarkMetrics& metrics, const std::size_t) {
        if (metrics.correctness_checks > 0U) {
            ImGui::Text("%u / %u", metrics.correctness_mismatches, metrics.correctness_checks);
        } else {
            ImGui::TextUnformatted("-");
        }
    });

    ImGui::EndTable();
}

void pushBenchmarkPanelTextWrap() noexcept {
    ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);
}

void popBenchmarkPanelTextWrap() noexcept {
    ImGui::PopTextWrapPos();
}

void drawBenchmarkWrappedBulletText(const char* text) {
    ImGui::Bullet();
    ImGui::SameLine(0.0F, 0.0F);
    pushBenchmarkPanelTextWrap();
    ImGui::TextUnformatted(text);
    popBenchmarkPanelTextWrap();
}

void drawBenchmarkPolicySkipPanel(
    const hbrick::ReachabilityBenchmarkReport& report
) {
    bool has_skips = false;
    for (const hbrick::BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.status == hbrick::BaselineStatus::SkippedByPolicy
            && !metrics.policy_skip_detail.empty()) {
            has_skips = true;
            break;
        }
    }

    if (!has_skips) {
        return;
    }

    ImGui::SeparatorText("Skipped by policy");
    for (const hbrick::BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.status != hbrick::BaselineStatus::SkippedByPolicy
            || metrics.policy_skip_detail.empty()) {
            continue;
        }

        char line[1024];
        std::snprintf(
            line,
            sizeof(line),
            "%s: %s",
            hbrick::reachabilityBaselineName(metrics.method),
            metrics.policy_skip_detail.c_str()
        );
        drawBenchmarkWrappedBulletText(line);
    }
}

void drawBenchmarkClosureHelpPanel(
    const hbrick::ReachabilityBenchmarkReport& report,
    const bool running
) {
    if (report.num_vertices == 0U && !running) {
        return;
    }

    ImGui::SeparatorText("Closure preprocessing");
    pushBenchmarkPanelTextWrap();
    ImGui::TextWrapped(
        "SccDagClosure builds a bit-matrix transitive closure on the condensation DAG "
        "(C nodes). FullClosure closes the full |V|×|V| reachability matrix. "
        "Both use Floyd-Warshall: outer loop pivot k = 0 … C−1 (or |V|−1). "
        "That is not repeated matrix multiplication A^k; each pivot merges paths "
        "through vertex k."
    );
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Projected total speedup (~Total x during preprocess) = "
        "CsrBfs end-to-end time ÷ projected closure end-to-end time."
    );
    ImGui::TextWrapped(
        "Projected closure time = setup (SCC + adjacency build) "
        "+ extrapolated remaining Warshall pivots "
        "+ same warmup as CsrBfs + estimated O(1) closure query time."
    );
    ImGui::TextWrapped(
        "Extrapolation: (pivot elapsed × pivot total ÷ pivots done). "
        "Auto-stop triggers when this projected speedup falls below 0.5× "
        "(only if enabled before Run). Use Skip closure method to stop manually."
    );
    popBenchmarkPanelTextWrap();
}

void drawBenchmarkDensityReference(const OrientationState& orient) {
    if (orient.density.valid) {
        if (orient.density.std_error == 0.0F) {
            ImGui::TextWrapped(
                "Estimated reachable-pair density: %.1f%% (%u BFS sources). "
                "Compare to the Reach %% row below.",
                static_cast<double>(orient.density.density) * 100.0,
                orient.density.samples
            );
        } else {
            ImGui::TextWrapped(
                "Estimated reachable-pair density: %.1f%% ± %.1f%% (%u BFS sources). "
                "Compare to the Reach %% row below.",
                static_cast<double>(orient.density.density) * 100.0,
                static_cast<double>(2.0F * orient.density.std_error) * 100.0,
                orient.density.samples
            );
        }
    } else {
        ImGui::TextDisabled(
            "No density estimate yet. Run one in the Orientation panel to compare "
            "against the Reach %% row."
        );
    }
}

void formatBenchmarkStageDetail(
    char* buffer,
    const std::size_t size,
    const hbrick::ReachabilityBenchmarkProgress& progress
) {
    const char* method_name =
        hbrick::reachabilityBaselineName(progress.current_method);

    switch (progress.stage) {
        case hbrick::ReachabilityBenchmarkProgress::Stage::GeneratingPairs:
            std::snprintf(buffer, size, "Generating shared query pairs");
            break;
        case hbrick::ReachabilityBenchmarkProgress::Stage::Preprocessing:
            if (progress.current_method
                    == hbrick::ReachabilityBaselineId::SccDagClosure
                || progress.current_method
                    == hbrick::ReachabilityBaselineId::FullClosure) {
                std::snprintf(
                    buffer,
                    size,
                    "Preprocessing %s  Warshall k=%u/%u  (method %u / %u)",
                    method_name,
                    progress.stage_work_completed,
                    progress.stage_work_total,
                    std::min(progress.methods_completed + 1U, progress.methods_total),
                    progress.methods_total
                );
            } else {
                std::snprintf(
                    buffer,
                    size,
                    "Preprocessing %s  (method %u / %u)",
                    method_name,
                    std::min(progress.methods_completed + 1U, progress.methods_total),
                    progress.methods_total
                );
            }
            break;
        case hbrick::ReachabilityBenchmarkProgress::Stage::WarmingUp:
            std::snprintf(
                buffer,
                size,
                "Warmup on %s  %u / %u",
                method_name,
                progress.stage_work_completed,
                progress.stage_work_total
            );
            break;
        case hbrick::ReachabilityBenchmarkProgress::Stage::Querying:
            std::snprintf(
                buffer,
                size,
                "Timed queries on %s  %u / %u",
                method_name,
                progress.stage_work_completed,
                progress.stage_work_total
            );
            break;
        case hbrick::ReachabilityBenchmarkProgress::Stage::CorrectnessCheck:
            std::snprintf(
                buffer,
                size,
                "Correctness checks on %s  %u / %u",
                method_name,
                progress.stage_work_completed,
                progress.stage_work_total
            );
            break;
        default:
            std::snprintf(buffer, size, "%s", benchmarkStageLabel(progress.stage));
            break;
    }
}

void drawBenchmarkFollowupDensityProgress(
    OrientationState& orient,
    const MazeLayout& layout
) {
    hbrick::ReachabilityDensityEstimator& estimator = orient.density_estimator;
    if (!orient.benchmark_followup_density || !estimator.active()) {
        return;
    }

    const auto step_start = std::chrono::steady_clock::now();
    constexpr auto kBudget = std::chrono::milliseconds(12);
    while (estimator.active()) {
        if (stepDensityEstimateParallel(orient, layout)) {
            break;
        }
        if (std::chrono::steady_clock::now() - step_start >= kBudget) {
            break;
        }
    }

    ImGui::SeparatorText("Reachability density");
    const float fraction = estimator.source_count() > 0U
        ? static_cast<float>(estimator.completed())
            / static_cast<float>(estimator.source_count())
        : 0.0F;
    char overlay[64];
    std::snprintf(
        overlay,
        sizeof(overlay),
        "%u / %u sources",
        estimator.completed(),
        estimator.source_count()
    );
    ImGui::ProgressBar(fraction, ImVec2(-1.0F, 0.0F), overlay);

    const hbrick::ReachabilityDensityEstimate running = estimator.snapshot();
    if (running.valid) {
        if (running.std_error == 0.0F) {
            ImGui::TextWrapped(
                "Density so far: %.1f%% (exact, %u sources)",
                static_cast<double>(running.density) * 100.0,
                running.samples
            );
        } else {
            ImGui::TextWrapped(
                "Density so far: %.1f%% ± %.1f%% (%u sources)",
                static_cast<double>(running.density) * 100.0,
                static_cast<double>(2.0F * running.std_error) * 100.0,
                running.samples
            );
        }
    } else {
        ImGui::TextDisabled("Estimating reachable-pair density for comparison…");
    }
}

}  // namespace

void BrowserApp::drawBenchmarkModals() {
    for (const std::unique_ptr<MapPanel>& panel_ptr : panels_) {
        MapPanel& panel = *panel_ptr;
        OrientationState& orient = panel.orient;
        if (!orient.benchmark_show_modal) {
            continue;
        }

        reapBenchmarkWorker(orient);

        char popup_id[96];
        std::snprintf(
            popup_id,
            sizeof(popup_id),
            "Running baseline benchmark###benchmark_job_%llu",
            static_cast<unsigned long long>(panel.id)
        );

        if (orient.benchmark_modal_requested) {
            orient.benchmark_modal_requested = false;
            ImGui::OpenPopup(popup_id);
        }
        if (benchmarkWorkerRunning(orient) || benchmarkJobActive(orient)) {
            ImGui::OpenPopup(popup_id);
        }

        ImGui::SetNextWindowSize(ImVec2(640.0F, 0.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::BeginPopupModal(popup_id, nullptr, ImGuiWindowFlags_None)) {
            continue;
        }

        ImGui::PushItemWidth(608.0F);

        const hbrick::ReachabilityBenchmarkProgress progress =
            orient.benchmark_job != nullptr
                ? orient.benchmark_job->progress()
                : hbrick::ReachabilityBenchmarkProgress{};
        const hbrick::ReachabilityBenchmarkReport& report =
            orient.benchmark_job != nullptr
                ? orient.benchmark_job->report()
                : hbrick::ReachabilityBenchmarkReport{};
        const bool worker_running = benchmarkWorkerRunning(orient);
        const bool running = worker_running || benchmarkJobActive(orient);
        const bool finished = !running && report.valid;
        const bool cancelled =
            !running
            && !report.valid
            && progress.stage == hbrick::ReachabilityBenchmarkProgress::Stage::Cancelled;
        const double elapsed_seconds = benchmarkElapsedSeconds(orient);

        if (finished
            && !orient.density.valid
            && !orient.density_estimator.active()
            && !orient.benchmark_followup_density) {
            orient.benchmark_followup_density = true;
            beginDensityEstimateFromPanelSettings(orient, panel.layout);
        }

        drawBenchmarkFollowupDensityProgress(orient, panel.layout);

        if (running) {
            ImGui::Text("Benchmark running%s", activityDots());
            ImGui::SameLine();
            ImGui::Text("(%.1f s elapsed)", elapsed_seconds);
        } else if (finished) {
            ImGui::TextWrapped("Benchmark complete (%.1f s).", elapsed_seconds);
        } else if (cancelled) {
            ImGui::TextWrapped("Benchmark cancelled (%.1f s).", elapsed_seconds);
        }

        if (running) {
            char stage_detail[160];
            formatBenchmarkStageDetail(stage_detail, sizeof(stage_detail), progress);
            ImGui::TextWrapped("%s", stage_detail);

            const float fraction = progress.work_total > 0U
                ? static_cast<float>(progress.work_completed)
                    / static_cast<float>(progress.work_total)
                : 0.0F;
            ImGui::ProgressBar(fraction, ImVec2(-1.0F, 20.0F), "");
            char steps_label[96];
            formatLargeCountPair(
                steps_label,
                sizeof(steps_label),
                progress.work_completed,
                progress.work_total
            );
            ImGui::Text(
                "%s steps (%.1f%%)",
                steps_label,
                static_cast<double>(fraction) * 100.0
            );

            ImGui::TextDisabled(
                "Heavy preprocessing may pause progress until a method finishes.\n"
                "Query phases update in small steps."
            );
        } else if (finished) {
            ImGui::ProgressBar(1.0F, ImVec2(-1.0F, 20.0F), "Complete");
        }

        if (!report.methods.empty() && (running || finished || cancelled)) {
            ImGui::SeparatorText(running ? "Live results" : "Results");
            if (report.valid) {
                ImGui::TextWrapped(
                    "Shared workload: %u pairs, seed %llu",
                    report.query_pair_count,
                    static_cast<unsigned long long>(report.pair_seed)
                );
            } else if (running) {
                ImGui::TextWrapped(
                    "Shared workload: %u pairs, seed %llu",
                    report.query_pair_count > 0U ? report.query_pair_count : progress.queries_total,
                    static_cast<unsigned long long>(orient.benchmark_config.pair_seed)
                );
            }

            drawBenchmarkDensityReference(orient);
            drawBenchmarkResultsTable(report, progress, running, "benchmark_modal_results");
            drawBenchmarkPolicySkipPanel(report);
            drawBenchmarkClosureHelpPanel(report, running);
        }

        if (running) {
            if (benchmarkClosurePreprocessActive(progress, running)) {
                if (ImGui::Button("Skip closure method", ImVec2(180.0F, 0.0F))) {
                    skipCurrentBenchmarkMethod(orient);
                }
                itemTooltip(
                    "Stop Warshall preprocessing for the active closure baseline and "
                    "move to the next method. Partial closure cannot answer queries."
                );
                ImGui::SameLine();
            }
            if (ImGui::Button("Cancel", ImVec2(120.0F, 0.0F))) {
                cancelReachabilityBenchmark(orient);
            }
        } else if (!worker_running) {
            const bool density_running =
                orient.benchmark_followup_density && orient.density_estimator.active();
            ImGui::BeginDisabled(density_running);
            if (ImGui::Button("Close", ImVec2(120.0F, 0.0F))) {
                if (density_running) {
                    orient.density_modal_requested = true;
                }
                orient.benchmark_followup_density = false;
                orient.benchmark_show_modal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (density_running) {
                ImGui::SameLine();
                ImGui::TextDisabled("Finishing density estimate…");
            }
        }

        ImGui::PopItemWidth();

        ImGui::EndPopup();
    }
}

void BrowserApp::drawDensityEstimateModals() {
    for (const std::unique_ptr<MapPanel>& panel_ptr : panels_) {
        MapPanel& panel = *panel_ptr;
        OrientationState& orient = panel.orient;
        ReachabilityDensityEstimator& estimator = orient.density_estimator;
        if (!estimator.active() && !orient.density_modal_requested) {
            continue;
        }
        if (orient.benchmark_show_modal && orient.benchmark_followup_density) {
            continue;
        }

        char popup_id[80];
        std::snprintf(
            popup_id,
            sizeof(popup_id),
            "Estimating reachable-pair density###density_job_%llu",
            static_cast<unsigned long long>(panel.id)
        );

        if (orient.density_modal_requested) {
            orient.density_modal_requested = false;
        }
        if (estimator.active()) {
            ImGui::OpenPopup(popup_id);
        }

        const auto start = std::chrono::steady_clock::now();
        constexpr auto kBudget = std::chrono::milliseconds(12);
        while (estimator.active()) {
            if (stepDensityEstimateParallel(orient, panel.layout)) {
                break;
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= kBudget) {
                break;
            }
        }

        if (!estimator.active()) {
            continue;
        }

        ImGui::SetNextWindowSize(ImVec2(380.0F, 0.0F), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(
                popup_id,
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            const float fraction = estimator.source_count() > 0U
                ? static_cast<float>(estimator.completed())
                    / static_cast<float>(estimator.source_count())
                : 0.0F;
            char overlay[64];
            std::snprintf(
                overlay,
                sizeof(overlay),
                "%u / %u sources",
                estimator.completed(),
                estimator.source_count()
            );
            ImGui::ProgressBar(fraction, ImVec2(-1.0F, 0.0F), overlay);

            const ReachabilityDensityEstimate running = estimator.snapshot();
            if (running.valid) {
                if (running.std_error == 0.0F) {
                    ImGui::TextWrapped(
                        "Density so far: %.4f (exact, %u sources)",
                        static_cast<double>(running.density),
                        running.samples
                    );
                } else {
                    ImGui::TextWrapped(
                        "Density so far: %.4f +/- %.4f (%u sources)",
                        static_cast<double>(running.density),
                        static_cast<double>(2.0F * running.std_error),
                        running.samples
                    );
                }
            } else {
                ImGui::TextDisabled("Density so far: —");
            }

            const bool auto_stop =
                estimator.sample_mode() == ReachabilityDensitySampleMode::AutoStopWhenStable;
            if (auto_stop) {
                if (estimator.exhaustive()) {
                    ImGui::TextWrapped(
                        "Auto-stop when stable (max %zu passable cells)",
                        estimator.universe().size()
                    );
                } else {
                    ImGui::TextWrapped(
                        "Auto-stop when stable (max %u random sources)",
                        estimator.source_count()
                    );
                }
            } else if (estimator.exhaustive()) {
                ImGui::TextWrapped(
                    "Exact estimate over all %zu passable cells",
                    estimator.universe().size()
                );
            } else {
                ImGui::TextWrapped(
                    "Sampling %u random passable sources",
                    estimator.source_count()
                );
            }

            if (orient.density_use_parallel && estimator.num_threads() > 1U) {
                ImGui::TextWrapped(
                    "Parallel workers: %u (distinct sources per batch)",
                    estimator.num_threads()
                );
            }

            ImGui::Separator();
            ImGui::BeginDisabled(estimator.completed() == 0U);
            if (ImGui::Button("Stop", ImVec2(120.0F, 0.0F))) {
                stopDensityEstimate(orient);
            }
            ImGui::EndDisabled();
            itemTooltip(
                "Keep the estimate from sources completed so far.\n"
                "Useful when the value has stabilized."
            );

            ImGui::EndPopup();
        }
    }
}

void BrowserApp::refreshRecipeIndex() {
    recipe_counts_.assign(index_.sets.size(), {});
    for (std::size_t i = 0; i < index_.sets.size(); ++i) {
        recipe_counts_[i].assign(index_.sets[i].maps.size(), 0U);
    }

    for (const std::filesystem::path& file : listRecipes(recipes_dir_)) {
        const std::optional<Recipe> recipe = loadRecipe(file);
        if (!recipe.has_value()) {
            continue;
        }
        for (std::size_t i = 0; i < index_.sets.size(); ++i) {
            if (index_.sets[i].name != recipe->set_name) {
                continue;
            }
            for (std::size_t j = 0; j < index_.sets[i].maps.size(); ++j) {
                if (index_.sets[i].maps[j].name == recipe->map_name) {
                    ++recipe_counts_[i][j];
                    break;
                }
            }
            break;
        }
    }
}

uint32_t BrowserApp::recipeCountFor(
    const std::size_t set_index,
    const std::size_t map_index
) const {
    if (set_index >= recipe_counts_.size()
        || map_index >= recipe_counts_[set_index].size()) {
        return 0U;
    }
    return recipe_counts_[set_index][map_index];
}

void BrowserApp::savePanelRecipe(MapPanel& panel) {
    const OrientationState& orient = panel.orient;
    Recipe recipe;
    recipe.set_name = panel.set_name;
    recipe.map_name = panel.map_name;
    recipe.policy = policy_;
    recipe.mode = orient.mode;
    recipe.seed = orient.seed;
    recipe.p_one_way = orient.p_one_way;
    recipe.p_bidirectional = orient.p_bidirectional;
    recipe.gradient_angle_degrees = orient.gradient_angle_degrees;
    recipe.p_against_gradient = orient.p_against_gradient;

    const std::filesystem::path written = saveRecipe(recipes_dir_, recipe);
    if (written.empty()) {
        status_ = "Recipe save failed (" + recipes_dir_.string() + ")";
        return;
    }

    status_ = "Saved recipe " + written.filename().string();
    markMapRecipesDirty(panel.set_name, panel.map_name);
    refreshRecipeIndex();
}

void BrowserApp::markMapRecipesDirty(
    const std::string& set_name,
    const std::string& map_name
) {
    for (const std::unique_ptr<MapPanel>& panel : panels_) {
        if (panel->set_name == set_name && panel->map_name == map_name) {
            panel->recipes_dirty = true;
        }
    }
}

void BrowserApp::deletePanelRecipe(
    MapPanel& panel,
    const std::filesystem::path& file
) {
    if (!deleteRecipe(file)) {
        status_ = "Recipe delete failed: " + file.filename().string();
        return;
    }

    status_ = "Deleted recipe " + file.filename().string();
    markMapRecipesDirty(panel.set_name, panel.map_name);
    refreshRecipeIndex();
}

void BrowserApp::applyRecipeToPanel(MapPanel& panel, const Recipe& recipe) {
    if (policy_ != recipe.policy) {
        policy_ = recipe.policy;
        rebuildAllLayouts();
    }

    OrientationState& orient = panel.orient;
    orient.mode = recipe.mode;
    orient.seed = recipe.seed;
    orient.p_one_way = recipe.p_one_way;
    orient.p_bidirectional = recipe.p_bidirectional;
    orient.gradient_angle_degrees = recipe.gradient_angle_degrees;
    orient.p_against_gradient = recipe.p_against_gradient;
    orient.editor_open = true;

    regenerateGraph(orient, panel.layout);
    computeScc(orient, panel.layout);
    panel.view_mode = ViewMode::Scc;
    rebuildPanelTexture(panel);

    status_ = "Applied recipe: " + recipe.set_name + "/" + recipe.map_name
        + ", " + std::to_string(orient.num_components) + " SCCs";
}

void BrowserApp::applyRecipe(const Recipe& recipe) {
    std::size_t set_index = index_.sets.size();
    std::size_t map_index = 0;
    for (std::size_t i = 0; i < index_.sets.size(); ++i) {
        if (index_.sets[i].name != recipe.set_name) {
            continue;
        }
        const SetEntry& set = index_.sets[i];
        for (std::size_t j = 0; j < set.maps.size(); ++j) {
            if (set.maps[j].name == recipe.map_name) {
                set_index = i;
                map_index = j;
                break;
            }
        }
        break;
    }
    if (set_index == index_.sets.size()) {
        status_ = "Recipe map not found in dataset: " + recipe.set_name + "/"
            + recipe.map_name;
        return;
    }

    openMap(set_index, map_index, true);
    MapPanel* panel = activePanel();
    if (panel == nullptr) {
        return;
    }
    applyRecipeToPanel(*panel, recipe);
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

    int view = 0;
    if (active != nullptr) {
        view = active->view_mode == ViewMode::Passability ? 1
            : active->view_mode == ViewMode::Scc ? 2 : 0;
    }
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
    ImGui::BeginDisabled(active == nullptr || !active->orient.scc_valid);
    if (ImGui::RadioButton("SCC components", &view, 2) && active != nullptr
        && active->view_mode != ViewMode::Scc) {
        active->view_mode = ViewMode::Scc;
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

        if (passable && active->orient.scc_valid) {
            const uint32_t component = active->orient.component_of[vertex.value];
            ImGui::Text(
                "SCC: id %u, size %u",
                component,
                active->orient.component_size_of[component]
            );
        }
        if (passable && active->orient.probe_valid) {
            ImGui::Text(
                "Probe reachable: %s",
                active->orient.probe_reachable[vertex.value] != 0U ? "yes" : "no"
            );
        }
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
