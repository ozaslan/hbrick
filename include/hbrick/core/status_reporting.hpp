/**
 * @file status_reporting.hpp
 * @ingroup hbrick_core
 * @brief Human-readable string conversion for core enumerations.
 */

#pragma once

#include <string_view>

#include "hbrick/core/types.hpp"

namespace hbrick {

/**
 * @brief Converts a reachability answer to a stable diagnostic label.
 * @ingroup hbrick_core
 *
 * @param answer Reachability result to stringify.
 * @return A non-owning view of a literal string such as @c "Reachable".
 */
[[nodiscard]] std::string_view toString(ReachabilityAnswer answer) noexcept;

/**
 * @brief Converts a baseline status to a stable diagnostic label.
 * @ingroup hbrick_core
 *
 * @param status Baseline lifecycle status to stringify.
 * @return A non-owning view of a literal string such as @c "Completed".
 */
[[nodiscard]] std::string_view toString(BaselineStatus status) noexcept;

}  // namespace hbrick
