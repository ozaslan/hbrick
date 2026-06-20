/**
 * @file hbrick_build_format.hpp
 * @ingroup hbrick_tile
 * @brief Text formatting helpers for H-BRICK build progress and reports.
 */

#pragma once

#include <cstddef>

#include "hbrick/tile/hbrick_build_report.hpp"

namespace hbrick {

/** @brief Human-readable label for @ref HBrickBuildStage. @ingroup hbrick_tile */
[[nodiscard]] const char* hbrickBuildStageLabel(HBrickBuildStage stage) noexcept;

/** @brief Formats nanoseconds as milliseconds with one decimal place. @ingroup hbrick_tile */
void formatHBrickBuildNanoseconds(
    char* buffer,
    std::size_t size,
    uint64_t nanoseconds
) noexcept;

/** @brief Formats byte counts using binary SI units. @ingroup hbrick_tile */
void formatHBrickBuildBytes(
    char* buffer,
    std::size_t size,
    uint64_t bytes
) noexcept;

/** @brief One-line progress detail for the active build stage. @ingroup hbrick_tile */
void formatHBrickBuildProgressDetail(
    char* buffer,
    std::size_t size,
    const HBrickBuildProgress& progress
) noexcept;

}  // namespace hbrick
