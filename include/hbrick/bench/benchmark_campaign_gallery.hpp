/**
 * @file benchmark_campaign_gallery.hpp
 * @ingroup hbrick_bench
 * @brief Headless passability gallery export for benchmark campaigns.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace hbrick {

class MazeLayout;

/**
 * @brief Writes an ASCII PPM passability image for @p layout.
 * @ingroup hbrick_bench
 *
 * Passable cells are light; blocked cells are dark (matches dataset browser palette).
 */
[[nodiscard]] bool writePassabilityPpm(
    const MazeLayout& layout,
    const std::filesystem::path& output_path,
    std::string& error_message
);

/** @brief FNV-1a hash of file contents; returns @c 0 when the file cannot be read. @ingroup hbrick_bench */
[[nodiscard]] uint64_t hashFileContentsFnv1a(
    const std::filesystem::path& path
) noexcept;

}  // namespace hbrick
