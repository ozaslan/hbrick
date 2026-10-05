/**
 * @file movingai_loader.hpp
 * @ingroup hbrick_io
 * @brief Parsing of MovingAI benchmark @c .map files into @ref hbrick::MovingAiMap.
 */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "hbrick/io/movingai_map.hpp"

namespace hbrick {

/**
 * @brief Outcome of loading or parsing a MovingAI map.
 * @ingroup hbrick_io
 *
 * On failure @ref error is non-empty and @ref map is empty. This is cold-path
 * io code, so descriptive error strings are intentional.
 */
struct MovingAiLoadResult {
    /** @brief Parsed map; valid only when @ref ok() returns @c true. @ingroup hbrick_io */
    MovingAiMap map;
    /** @brief Empty on success, otherwise a human-readable failure description. @ingroup hbrick_io */
    std::string error;

    /** @brief Returns whether loading succeeded. @return @c true when @ref error is empty. @ingroup hbrick_io */
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

/**
 * @brief Parses MovingAI @c .map content from an in-memory string.
 * @ingroup hbrick_io
 *
 * Accepts the documented format: a header with @c type, @c height, and
 * @c width fields (in any order) terminated by a @c map line, followed by
 * @c height rows of at least @c width cell characters. Carriage returns and
 * trailing whitespace are tolerated.
 *
 * @param text Full file content.
 * @return Result carrying the parsed map or an error description.
 */
[[nodiscard]] MovingAiLoadResult parseMovingAiMap(std::string_view text);

/**
 * @brief Loads and parses a MovingAI @c .map file from disk.
 * @ingroup hbrick_io
 *
 * @param path Path to the @c .map file.
 * @return Result carrying the parsed map or an error description.
 */
[[nodiscard]] MovingAiLoadResult loadMovingAiMap(const std::filesystem::path& path);

}  // namespace hbrick
