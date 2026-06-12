/**
 * @file map_render.hpp
 * @brief Conversion of MovingAI maps and maze layouts to OpenGL textures.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/io/movingai_map.hpp"

namespace hbrick::tools {

/** @brief Owning handle for one OpenGL texture (RGBA8, nearest-neighbor). */
struct GlTexture {
    uint32_t id = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    [[nodiscard]] bool valid() const noexcept { return id != 0; }
};

/** @brief RGBA color of one raw MovingAI terrain character. */
void terrainColor(char cell, uint8_t out[4]) noexcept;

/** @brief Builds row-major RGBA pixels colored by raw terrain class. */
[[nodiscard]] std::vector<uint8_t> renderTerrainPixels(const MovingAiMap& map);

/**
 * @brief Builds row-major RGBA pixels colored by MazeLayout passability.
 *
 * This rendering path deliberately reads the converted @ref hbrick::MazeLayout
 * (not the raw map) so the GUI exercises the library container end to end.
 */
[[nodiscard]] std::vector<uint8_t> renderPassabilityPixels(const MazeLayout& layout);

/**
 * @brief Downsamples full-resolution pixels to a thumbnail no larger than
 *        @p max_edge on its longest side, using nearest sampling.
 */
[[nodiscard]] std::vector<uint8_t> downsamplePixels(
    const std::vector<uint8_t>& pixels,
    uint32_t width,
    uint32_t height,
    uint32_t max_edge,
    uint32_t& out_width,
    uint32_t& out_height
);

/** @brief Uploads RGBA8 pixels as a GL_NEAREST texture; replaces @p texture. */
void uploadTexture(GlTexture& texture, const std::vector<uint8_t>& pixels,
                   uint32_t width, uint32_t height);

/** @brief Deletes the GL texture if present and resets the handle. */
void destroyTexture(GlTexture& texture) noexcept;

}  // namespace hbrick::tools
