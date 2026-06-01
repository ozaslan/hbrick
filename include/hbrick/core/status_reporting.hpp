#pragma once

#include <string_view>

#include "hbrick/core/types.hpp"

namespace hbrick {

[[nodiscard]] std::string_view toString(ReachabilityAnswer answer) noexcept;
[[nodiscard]] std::string_view toString(BaselineStatus status) noexcept;

}  // namespace hbrick
