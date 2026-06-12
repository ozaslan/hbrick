#include "browser_app.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "hbrick/io/movingai_loader.hpp"

namespace hbrick::tools {

namespace {

constexpr uint32_t kThumbnailEdge = 64U;
constexpr float kThumbnailDrawEdge = 52.0F;
constexpr float kMinZoom = 0.05F;
constexpr float kMaxZoom = 64.0F;

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
            + std::to_string(index_.sets.size()) + " sets. Select a map to view.";
    }
}

BrowserApp::~BrowserApp() {
    for (std::vector<GlTexture>& set : thumbnails_) {
        for (GlTexture& texture : set) {
            destroyTexture(texture);
        }
    }
    if (opened_.has_value()) {
        destroyTexture(opened_->texture);
    }
}

void BrowserApp::drawFrame() {
    ImGui::DockSpaceOverViewport(0U, ImGui::GetMainViewport());

    drawGalleryPanel();
    drawCanvasPanel();
    drawInspectorPanel();
}

void BrowserApp::openMap(const std::size_t set_index, const std::size_t map_index) {
    const SetEntry& set = index_.sets[set_index];
    const MapEntry& entry = set.maps[map_index];

    MovingAiLoadResult result = loadMovingAiMap(entry.path);
    if (!result.ok()) {
        status_ = "Load failed: " + result.error;
        return;
    }

    OpenedMap opened;
    opened.set_name = set.name;
    opened.map_name = entry.name;
    opened.map = std::move(result.map);
    opened.layout = opened.map.toMazeLayout(policy_);

    if (opened_.has_value()) {
        destroyTexture(opened_->texture);
    }
    opened_ = std::move(opened);
    selected_set_ = set_index;
    selected_map_ = map_index;
    fit_requested_ = true;

    rebuildMapTexture();
    status_ = set.name + "/" + entry.name + ": "
        + std::to_string(opened_->map.width()) + "x"
        + std::to_string(opened_->map.height()) + ", "
        + std::to_string(opened_->layout.passableCount()) + " passable cells";
}

void BrowserApp::rebuildMapTexture() {
    if (!opened_.has_value()) {
        return;
    }
    const std::vector<uint8_t> pixels = view_mode_ == ViewMode::Terrain
        ? renderTerrainPixels(opened_->map)
        : renderPassabilityPixels(opened_->layout);
    uploadTexture(
        opened_->texture,
        pixels,
        opened_->map.width(),
        opened_->map.height()
    );
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

void BrowserApp::drawGalleryPanel() {
    ImGui::Begin("Datasets");

    for (std::size_t set_index = 0; set_index < index_.sets.size(); ++set_index) {
        const SetEntry& set = index_.sets[set_index];
        const std::string header =
            set.name + " (" + std::to_string(set.maps.size()) + ")";
        if (!ImGui::CollapsingHeader(header.c_str())) {
            continue;
        }

        ImGui::PushID(static_cast<int>(set_index));
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(set.maps.size()),
                      kThumbnailDrawEdge + 6.0F);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto map_index = static_cast<std::size_t>(row);
                const MapEntry& entry = set.maps[map_index];
                ImGui::PushID(row);

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

                const bool is_selected = opened_.has_value()
                    && selected_set_ == set_index && selected_map_ == map_index;
                if (ImGui::Selectable(
                        entry.name.c_str(),
                        is_selected,
                        0,
                        ImVec2(0.0F, kThumbnailDrawEdge))) {
                    openMap(set_index, map_index);
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }

    ImGui::End();
}

void BrowserApp::resetViewToFit(const float canvas_width, const float canvas_height) {
    if (!opened_.has_value() || opened_->map.width() == 0U) {
        return;
    }
    const float map_width = static_cast<float>(opened_->map.width());
    const float map_height = static_cast<float>(opened_->map.height());
    zoom_ = std::min(canvas_width / map_width, canvas_height / map_height);
    zoom_ = std::clamp(zoom_, kMinZoom, kMaxZoom);
    pan_x_ = (canvas_width - map_width * zoom_) * 0.5F;
    pan_y_ = (canvas_height - map_height * zoom_) * 0.5F;
}

void BrowserApp::drawCanvasPanel() {
    ImGui::Begin("Canvas");

    if (!opened_.has_value()) {
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::End();
        return;
    }

    if (ImGui::Button("Fit")) {
        fit_requested_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("1:1")) {
        zoom_ = 1.0F;
    }
    ImGui::SameLine();
    ImGui::Text("zoom %.2fx   %s", static_cast<double>(zoom_), status_.c_str());

    const ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 16.0F || canvas_size.y < 16.0F) {
        ImGui::End();
        return;
    }

    if (fit_requested_) {
        resetViewToFit(canvas_size.x, canvas_size.y);
        fit_requested_ = false;
    }

    ImGui::InvisibleButton(
        "canvas",
        canvas_size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle
    );
    const bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();

    // Pan with left or middle drag.
    if (ImGui::IsItemActive()
        && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F)
            || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0F))) {
        pan_x_ += io.MouseDelta.x;
        pan_y_ += io.MouseDelta.y;
    }

    // Wheel zoom anchored at the cursor, Photoshop style.
    if (hovered && io.MouseWheel != 0.0F) {
        const float new_zoom = std::clamp(
            zoom_ * std::pow(1.15F, io.MouseWheel),
            kMinZoom,
            kMaxZoom
        );
        const float mouse_x = io.MousePos.x - canvas_pos.x;
        const float mouse_y = io.MousePos.y - canvas_pos.y;
        const float world_x = (mouse_x - pan_x_) / zoom_;
        const float world_y = (mouse_y - pan_y_) / zoom_;
        pan_x_ = mouse_x - world_x * new_zoom;
        pan_y_ = mouse_y - world_y * new_zoom;
        zoom_ = new_zoom;
    }

    const float map_width = static_cast<float>(opened_->map.width());
    const float map_height = static_cast<float>(opened_->map.height());
    const ImVec2 image_min(canvas_pos.x + pan_x_, canvas_pos.y + pan_y_);
    const ImVec2 image_max(
        image_min.x + map_width * zoom_,
        image_min.y + map_height * zoom_
    );

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(
        canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        true
    );
    draw_list->AddRectFilled(
        canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        IM_COL32(18, 18, 22, 255)
    );
    if (opened_->texture.valid()) {
        draw_list->AddImage(toImTexture(opened_->texture), image_min, image_max);
    }

    // Eyedropper: resolve the cell under the cursor.
    hover_valid_ = false;
    if (hovered) {
        const float cell_x = (io.MousePos.x - image_min.x) / zoom_;
        const float cell_y = (io.MousePos.y - image_min.y) / zoom_;
        if (cell_x >= 0.0F && cell_y >= 0.0F
            && cell_x < map_width && cell_y < map_height) {
            hover_valid_ = true;
            hover_x_ = static_cast<uint32_t>(cell_x);
            hover_y_ = static_cast<uint32_t>(cell_y);

            // Highlight the hovered cell once zoomed in enough to see it.
            if (zoom_ >= 3.0F) {
                const ImVec2 cell_min(
                    image_min.x + static_cast<float>(hover_x_) * zoom_,
                    image_min.y + static_cast<float>(hover_y_) * zoom_
                );
                const ImVec2 cell_max(cell_min.x + zoom_, cell_min.y + zoom_);
                draw_list->AddRect(cell_min, cell_max,
                                   IM_COL32(255, 200, 0, 255), 0.0F, 0, 1.5F);
            }
        }
    }
    draw_list->PopClipRect();

    ImGui::End();
}

void BrowserApp::drawInspectorPanel() {
    ImGui::Begin("Inspector");

    ImGui::SeparatorText("View");

    int view = view_mode_ == ViewMode::Terrain ? 0 : 1;
    bool texture_dirty = false;
    if (ImGui::RadioButton("Terrain colors", &view, 0)) {
        texture_dirty = view_mode_ != ViewMode::Terrain;
        view_mode_ = ViewMode::Terrain;
    }
    if (ImGui::RadioButton("MazeLayout passability", &view, 1)) {
        texture_dirty = view_mode_ != ViewMode::Passability;
        view_mode_ = ViewMode::Passability;
    }

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
                if (opened_.has_value()) {
                    opened_->layout = opened_->map.toMazeLayout(policy_);
                    texture_dirty = view_mode_ == ViewMode::Passability;
                }
            }
        }
        ImGui::EndCombo();
    }
    if (texture_dirty) {
        rebuildMapTexture();
    }

    ImGui::SeparatorText("Map");
    if (opened_.has_value()) {
        ImGui::Text("Set: %s", opened_->set_name.c_str());
        ImGui::Text("Map: %s", opened_->map_name.c_str());
        ImGui::Text("Type: %s", opened_->map.typeName().c_str());
        ImGui::Text("Size: %u x %u (%u cells)",
                    opened_->map.width(),
                    opened_->map.height(),
                    opened_->map.dimensions().numCells());
        ImGui::Text("Passable: %u cells", opened_->layout.passableCount());
    } else {
        ImGui::TextDisabled("No map opened");
    }

    ImGui::SeparatorText("Cell under cursor");
    if (opened_.has_value() && hover_valid_) {
        const GridCoord coord{hover_x_, hover_y_};
        const char cell = opened_->map.cellAt(coord);
        const MovingAiTerrain terrain = opened_->map.terrainAt(coord);
        const bool passable = opened_->layout.isPassable(coord);
        const VertexId vertex = opened_->layout.vertexId(coord);

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
        ImGui::TextDisabled("Hover the canvas");
    }

    ImGui::SeparatorText("About");
    ImGui::TextWrapped(
        "Read-only browser for the MovingAI 2D pathfinding benchmarks "
        "(Sturtevant 2012, ODC-BY). Maps are loaded through hbrick_io into "
        "MovingAiMap and converted to hbrick MazeLayout."
    );

    ImGui::End();
}

}  // namespace hbrick::tools
