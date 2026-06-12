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
#include "orientation.hpp"
#include "recipe.hpp"

namespace hbrick::tools {

/** @brief How a map panel is colored. */
enum class ViewMode : uint8_t {
    /** @brief Color cells by raw MovingAI terrain class. */
    Terrain,
    /** @brief Color cells by MazeLayout passability under the active policy. */
    Passability,
    /** @brief Color passable cells by strongly connected component id. */
    Scc,
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
    // True while the panel's window content is actually drawn this frame
    // (false when it is a hidden tab in a dock). Gates the orientation
    // editor so it never edits a map the user cannot see.
    bool visible_now = false;

    // Loupe anchor corner; only flips when the cursor approaches it.
    bool loupe_right = true;
    bool loupe_bottom = true;

    // Directed-orientation editor state (graph, SCCs, probe).
    OrientationState orient;

    // Saved recipes for this exact map, shown inside the orientation editor.
    std::vector<SavedRecipeEntry> map_recipes;
    bool recipes_dirty = true;
    std::filesystem::path recipe_delete_pending_;
    bool recipe_delete_modal_requested_ = false;
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

    /**
     * @brief Rebuilds the UI font atlas when the size changed.
     *
     * Call once per frame before @c ImGui::NewFrame. When this returns @c true,
     * the OpenGL backend must destroy and recreate its device objects so the
     * new font texture is uploaded.
     */
    void prepareFrame();
    [[nodiscard]] bool fontGpuRefreshNeeded() const noexcept {
        return font_gpu_refresh_needed_;
    }
    void clearFontGpuRefreshNeeded() noexcept { font_gpu_refresh_needed_ = false; }

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
    void drawOrientationEditor(MapPanel& panel);
    void drawEdgeArrows(
        ImDrawList* draw_list,
        const MapPanel& panel,
        ImVec2 origin,
        float scale_x,
        float scale_y,
        uint32_t x_first,
        uint32_t x_last,
        uint32_t y_first,
        uint32_t y_last
    ) const;

    void applyPreset(LayoutPreset preset, unsigned int dockspace_id);
    void handleShortcuts();
    void handleGlobalShortcuts();
    void handleMapShortcuts(MapPanel& panel);
    void adjustUiFont(float delta_pixels);
    void rebuildUiFont();
    void drawShortcutsWindow();
    void applyRecipe(const Recipe& recipe);
    void applyRecipeToPanel(MapPanel& panel, const Recipe& recipe);
    void refreshRecipeIndex();
    [[nodiscard]] uint32_t recipeCountFor(std::size_t set_index, std::size_t map_index) const;
    void setProbe(MapPanel& panel, uint32_t x, uint32_t y);
    void setComponentProbe(MapPanel& panel, uint32_t component);
    void savePanelRecipe(MapPanel& panel);
    void deletePanelRecipe(MapPanel& panel, const std::filesystem::path& file);
    void markMapRecipesDirty(const std::string& set_name, const std::string& map_name);

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
    bool show_shortcuts_ = false;
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
    bool filter_recipes_only_ = false;
    std::string status_;

    std::filesystem::path recipes_dir_;
    // Recipe counts parallel to index_.sets[i].maps[j]; drives gallery badges
    // and the recipes-only filter.
    std::vector<std::vector<uint32_t>> recipe_counts_;

    float ui_font_size_ = 16.0F;
    bool ui_font_dirty_ = true;
    bool font_gpu_refresh_needed_ = false;
};

}  // namespace hbrick::tools
