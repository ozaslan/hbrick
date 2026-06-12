/**
 * @file browser_app.hpp
 * @brief Read-only ImGui application state and per-frame UI for the dataset browser.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/io/movingai_map.hpp"

#include "dataset_index.hpp"
#include "map_render.hpp"

namespace hbrick::tools {

/** @brief How a map panel is colored. */
enum class ViewMode : uint8_t {
    /** @brief Color cells by raw MovingAI terrain class. */
    Terrain,
    /** @brief Color cells by MazeLayout passability under the active policy. */
    Passability,
};

/** @brief Built-in dock layout presets selectable from the menu bar. */
enum class LayoutPreset : uint8_t {
    /** @brief Gallery left, maps center, inspector right. */
    Browse,
    /** @brief Maps only; side panels hidden. */
    Focus,
    /** @brief Two map slots side by side, gallery and inspector tabbed left. */
    Compare,
};

/**
 * @brief One opened map panel (VS Code style: preview panels are reused by
 *        single clicks, pinned panels are dedicated via double click).
 */
struct MapPanel {
    uint64_t id = 0;
    std::string set_name;
    std::string map_name;
    MovingAiMap map;
    MazeLayout layout{0U, 0U};
    GlTexture texture;

    ViewMode view_mode = ViewMode::Terrain;
    float zoom = 1.0F;
    float pan_x = 0.0F;
    float pan_y = 0.0F;
    bool fit_requested = true;

    bool open = true;
    bool pinned = false;
    bool focus_requested = false;

    // Loupe anchor corner; only flips when the cursor approaches it.
    bool loupe_right = true;
    bool loupe_bottom = true;
};

/** @brief Dataset browser application; owns all UI state. Strictly read-only. */
class BrowserApp {
public:
    explicit BrowserApp(std::filesystem::path dataset_root);
    ~BrowserApp();

    BrowserApp(const BrowserApp&) = delete;
    BrowserApp& operator=(const BrowserApp&) = delete;

    /** @brief Draws the full UI for one frame. */
    void drawFrame();

    /** @brief True after File > Quit; the main loop closes the window. */
    [[nodiscard]] bool quitRequested() const noexcept { return quit_requested_; }

private:
    void drawMenuBar();
    void drawStatusBar();
    void drawGalleryPanel();
    void drawMapPanels();
    void drawPanelCanvas(MapPanel& panel);
    void drawLoupe(
        ImDrawList* draw_list,
        MapPanel& panel,
        ImVec2 canvas_pos,
        ImVec2 canvas_size,
        ImVec2 mouse_pos
    ) const;
    void drawInspectorPanel();

    void applyPreset(LayoutPreset preset, unsigned int dockspace_id);
    void handleShortcuts();

    void openMap(std::size_t set_index, std::size_t map_index, bool pinned);
    void closePanel(MapPanel& panel);
    void pruneClosedPanels();
    [[nodiscard]] MapPanel* activePanel() noexcept;
    void rebuildPanelTexture(MapPanel& panel);
    void rebuildAllLayouts();
    GlTexture& thumbnailFor(std::size_t set_index, std::size_t map_index);

    DatasetIndex index_;
    std::vector<std::vector<GlTexture>> thumbnails_;

    std::vector<std::unique_ptr<MapPanel>> panels_;
    uint64_t next_panel_id_ = 1;
    uint64_t active_panel_id_ = 0;

    MovingAiPassabilityPolicy policy_ = MovingAiPassabilityPolicy::GroundOnly;

    bool show_gallery_ = true;
    bool show_inspector_ = true;
    bool show_loupe_ = true;
    bool show_grid_ = true;
    bool quit_requested_ = false;

    bool preset_pending_ = true;
    LayoutPreset pending_preset_ = LayoutPreset::Browse;
    bool redock_panels_ = false;
    unsigned int dock_main_ = 0;
    unsigned int dock_alt_ = 0;

    bool hover_valid_ = false;
    uint32_t hover_x_ = 0;
    uint32_t hover_y_ = 0;

    char filter_[64] = {};
    std::string status_;
};

}  // namespace hbrick::tools
