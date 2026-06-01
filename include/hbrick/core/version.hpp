#pragma once

#include <string_view>

namespace hbrick {

inline constexpr std::string_view kLibraryVersion = "0.1.0";

const char* libraryVersion() noexcept;

}  // namespace hbrick
