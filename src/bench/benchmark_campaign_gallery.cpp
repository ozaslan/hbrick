#include "hbrick/bench/benchmark_campaign_gallery.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

#include "hbrick/grid/maze_layout.hpp"

namespace hbrick {

namespace {

constexpr uint8_t kBlockedRed = 28U;
constexpr uint8_t kBlockedGreen = 28U;
constexpr uint8_t kBlockedBlue = 32U;
constexpr uint8_t kPassableRed = 222U;
constexpr uint8_t kPassableGreen = 222U;
constexpr uint8_t kPassableBlue = 214U;

}  // namespace

bool writePassabilityPpm(
    const MazeLayout& layout,
    const std::filesystem::path& output_path,
    std::string& error_message
) {
    const uint32_t width = layout.width();
    const uint32_t height = layout.height();
    if (width == 0U || height == 0U) {
        error_message = "Cannot write gallery image for an empty layout";
        return false;
    }

    std::ofstream output(output_path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        error_message = "Failed to open gallery image for write: " + output_path.string();
        return false;
    }

    output << "P3\n" << width << ' ' << height << "\n255\n";
    for (uint32_t y = 0U; y < height; ++y) {
        for (uint32_t x = 0U; x < width; ++x) {
            if (layout.isPassable(x, y)) {
                output << static_cast<int>(kPassableRed) << ' '
                       << static_cast<int>(kPassableGreen) << ' '
                       << static_cast<int>(kPassableBlue) << ' ';
            } else {
                output << static_cast<int>(kBlockedRed) << ' '
                       << static_cast<int>(kBlockedGreen) << ' '
                       << static_cast<int>(kBlockedBlue) << ' ';
            }
        }
        output << '\n';
    }

    if (!output.good()) {
        error_message = "Failed to write gallery image: " + output_path.string();
        return false;
    }
    return true;
}

uint64_t hashFileContentsFnv1a(const std::filesystem::path& path) noexcept {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return 0U;
    }

    uint64_t hash = 0xCBF29CE484222325ULL;
    std::vector<char> buffer(4096U);
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytes_read = input.gcount();
        for (std::streamsize index = 0; index < bytes_read; ++index) {
            hash ^= static_cast<uint8_t>(buffer[static_cast<std::size_t>(index)]);
            hash *= 0x100000001B3ULL;
        }
    }
    return hash;
}

}  // namespace hbrick
