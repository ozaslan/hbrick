#include "map_render.hpp"

#include <GL/gl.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace hbrick::tools {

namespace {

void setRgba(uint8_t out[4], uint8_t r, uint8_t g, uint8_t b) noexcept {
    out[0] = r;
    out[1] = g;
    out[2] = b;
    out[3] = 255U;
}

}  // namespace

void terrainColor(const char cell, uint8_t out[4]) noexcept {
    switch (movingAiTerrainFromChar(cell)) {
        case MovingAiTerrain::Ground:
            setRgba(out, 222U, 222U, 214U);
            return;
        case MovingAiTerrain::Tree:
            setRgba(out, 36U, 110U, 50U);
            return;
        case MovingAiTerrain::Swamp:
            setRgba(out, 128U, 144U, 70U);
            return;
        case MovingAiTerrain::Water:
            setRgba(out, 62U, 96U, 190U);
            return;
        case MovingAiTerrain::OutOfBounds:
            setRgba(out, 28U, 28U, 32U);
            return;
        case MovingAiTerrain::WeightedTerrain: {
            // Spread A..Z over a cool-to-warm ramp so cost classes are visible.
            const uint8_t step = static_cast<uint8_t>(cell - 'A');
            const uint8_t warm = static_cast<uint8_t>(90U + step * 6U);
            const uint8_t cool = static_cast<uint8_t>(235U - step * 7U);
            setRgba(out, warm, static_cast<uint8_t>(120U + step * 2U), cool);
            return;
        }
        case MovingAiTerrain::Unknown:
        default:
            setRgba(out, 230U, 40U, 220U);
            return;
    }
}

std::vector<uint8_t> renderTerrainPixels(const MovingAiMap& map) {
    const uint32_t width = map.width();
    const uint32_t height = map.height();
    std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U);

    const std::vector<char>& cells = map.cells();
    for (std::size_t i = 0; i < cells.size(); ++i) {
        terrainColor(cells[i], pixels.data() + i * 4U);
    }
    return pixels;
}

std::vector<uint8_t> renderPassabilityPixels(const MazeLayout& layout) {
    const uint32_t width = layout.width();
    const uint32_t height = layout.height();
    std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width + x) * 4U;
            if (layout.isPassable(x, y)) {
                setRgba(pixels.data() + offset, 245U, 245U, 240U);
            } else {
                setRgba(pixels.data() + offset, 40U, 42U, 54U);
            }
        }
    }
    return pixels;
}

std::vector<uint8_t> downsamplePixels(
    const std::vector<uint8_t>& pixels,
    const uint32_t width,
    const uint32_t height,
    const uint32_t max_edge,
    uint32_t& out_width,
    uint32_t& out_height
) {
    const uint32_t longest = width > height ? width : height;
    if (longest <= max_edge || longest == 0U) {
        out_width = width;
        out_height = height;
        return pixels;
    }

    const float scale = static_cast<float>(max_edge) / static_cast<float>(longest);
    const auto scaled = [scale](const uint32_t value) {
        const uint32_t result =
            static_cast<uint32_t>(static_cast<float>(value) * scale);
        return result > 0U ? result : 1U;
    };
    out_width = scaled(width);
    out_height = scaled(height);

    std::vector<uint8_t> small(
        static_cast<std::size_t>(out_width) * out_height * 4U
    );
    for (uint32_t y = 0; y < out_height; ++y) {
        const uint32_t src_y = static_cast<uint32_t>(
            (static_cast<uint64_t>(y) * height) / out_height
        );
        for (uint32_t x = 0; x < out_width; ++x) {
            const uint32_t src_x = static_cast<uint32_t>(
                (static_cast<uint64_t>(x) * width) / out_width
            );
            const std::size_t src_offset =
                (static_cast<std::size_t>(src_y) * width + src_x) * 4U;
            const std::size_t dst_offset =
                (static_cast<std::size_t>(y) * out_width + x) * 4U;
            small[dst_offset + 0U] = pixels[src_offset + 0U];
            small[dst_offset + 1U] = pixels[src_offset + 1U];
            small[dst_offset + 2U] = pixels[src_offset + 2U];
            small[dst_offset + 3U] = pixels[src_offset + 3U];
        }
    }
    return small;
}

void uploadTexture(
    GlTexture& texture,
    const std::vector<uint8_t>& pixels,
    const uint32_t width,
    const uint32_t height
) {
    destroyTexture(texture);

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data()
    );
    glBindTexture(GL_TEXTURE_2D, 0);

    texture.id = id;
    texture.width = width;
    texture.height = height;
}

void destroyTexture(GlTexture& texture) noexcept {
    if (texture.id != 0U) {
        const GLuint id = texture.id;
        glDeleteTextures(1, &id);
    }
    texture = GlTexture{};
}

}  // namespace hbrick::tools
