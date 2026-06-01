#pragma once

#include <cstdint>

namespace hbrick {

struct Edge32 {
    uint32_t from = 0;
    uint32_t to = 0;

    constexpr Edge32() noexcept = default;

    constexpr Edge32(uint32_t from_vertex, uint32_t to_vertex) noexcept
        : from(from_vertex), to(to_vertex) {}

    friend constexpr bool operator==(Edge32 lhs, Edge32 rhs) noexcept {
        return lhs.from == rhs.from && lhs.to == rhs.to;
    }

    friend constexpr bool operator!=(Edge32 lhs, Edge32 rhs) noexcept {
        return !(lhs == rhs);
    }
};

}  // namespace hbrick
