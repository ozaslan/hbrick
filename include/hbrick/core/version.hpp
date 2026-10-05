/**
 * @file version.hpp
 * @ingroup hbrick_core
 * @brief Library version constants and accessors.
 */

#pragma once

#include <string_view>

namespace hbrick {

/**
 * @brief Semantic version string for the hbrick library.
 * @ingroup hbrick_core
 *
 * Updated manually alongside release tagging.
 */
inline constexpr std::string_view kLibraryVersion = "0.1.0";

/**
 * @brief Returns the library version as a null-terminated C string.
 * @ingroup hbrick_core
 *
 * The returned pointer remains valid for the lifetime of the program.
 *
 * @return Pointer to a static string equal to @ref hbrick::kLibraryVersion.
 */
const char* libraryVersion() noexcept;

}  // namespace hbrick
