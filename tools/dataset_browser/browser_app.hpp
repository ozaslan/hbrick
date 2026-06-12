/**
 * @file browser_app.hpp
 * @brief Read-only ImGui application state and per-frame UI for the dataset browser.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/io/movingai_map.hpp"

#include "dataset_index.hpp"
#include "map_render.hpp"

namespace hbrick::tools {

/** @brief How the selected map is colored in the canvas. */
enum class ViewMode : uint8_t {
    /** @brief Color cells by raw MovingAI terrain class. */
    Terrain,
    /** @brief Color cells by MazeLayout passability under the active policy. */
    Passability,
};

/** @brief Currently opened map with its derived library representations. */
struct OpenedMap {
    std::string set_name;
    std::string map_name;
    MovingAiMap map;
    MazeLayout layout{0U, 0U};
    GlTexture texture;
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

private:
    void drawGalleryPanel();
    void drawCanvasPanel();
    void drawInspectorPanel();

    void openMap(std::size_t set_index, std::size_t map_index);
    void rebuildMapTexture();
    GlTexture& thumbnailFor(std::size_t set_index, std::size_t map_index);
    void resetViewToFit(float canvas_width, float canvas_height);

    DatasetIndex index_;
    std::vector<std::vector<GlTexture>> thumbnails_;

    std::optional<OpenedMap> opened_;
    std::size_t selected_set_ = 0;
    std::size_t selected_map_ = 0;

    MovingAiPassabilityPolicy policy_ = MovingAiPassabilityPolicy::GroundOnly;
    ViewMode view_mode_ = ViewMode::Terrain;

    float zoom_ = 1.0F;
    float pan_x_ = 0.0F;
    float pan_y_ = 0.0F;
    bool fit_requested_ = true;

    bool hover_valid_ = false;
    uint32_t hover_x_ = 0;
    uint32_t hover_y_ = 0;

    std::string status_;
};

}  // namespace hbrick::tools
