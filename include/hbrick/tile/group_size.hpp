/**
 * @file group_size.hpp
 * @ingroup hbrick_tile
 * @brief Branching factor for grouping child tile slots into a parent region (H-BRICK).
 */

#pragma once

#include <cstdint>

namespace hbrick {

/**
 * @brief How many child slots form one parent along each axis at hierarchy level >= 1.
 * @ingroup hbrick_tile
 */
struct GroupSize {
    /** @brief Number of child slots along the tile-i axis. @ingroup hbrick_tile */
    uint32_t group_w = 2U;
    /** @brief Number of child slots along the tile-j axis. @ingroup hbrick_tile */
    uint32_t group_h = 2U;

    /** @brief Default-constructs a @c 2 x 2 grouping. @ingroup hbrick_tile */
    constexpr GroupSize() noexcept = default;

    /**
     * @brief Constructs a grouping size from explicit counts.
     * @ingroup hbrick_tile
     */
    constexpr GroupSize(uint32_t group_w_value, uint32_t group_h_value) noexcept
        : group_w(group_w_value), group_h(group_h_value) {}

    /**
     * @brief Returns whether both group counts are positive.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return group_w >= 1U && group_h >= 1U;
    }

    /** @brief Equality on both axes. @ingroup hbrick_tile */
    friend constexpr bool operator==(GroupSize lhs, GroupSize rhs) noexcept {
        return lhs.group_w == rhs.group_w && lhs.group_h == rhs.group_h;
    }

    /** @brief Inequality on both axes. @ingroup hbrick_tile */
    friend constexpr bool operator!=(GroupSize lhs, GroupSize rhs) noexcept {
        return !(lhs == rhs);
    }
};

}  // namespace hbrick
