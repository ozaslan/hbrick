#pragma once

#include <cstdint>
#include <limits>

namespace hbrick {

inline constexpr uint32_t kInvalidVertexId = std::numeric_limits<uint32_t>::max();

struct VertexId {
    uint32_t value = kInvalidVertexId;

    constexpr VertexId() noexcept = default;

    constexpr explicit VertexId(uint32_t id) noexcept : value(id) {}

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return value != kInvalidVertexId;
    }

    [[nodiscard]] static constexpr VertexId invalid() noexcept {
        return VertexId{kInvalidVertexId};
    }

    friend constexpr bool operator==(VertexId lhs, VertexId rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(VertexId lhs, VertexId rhs) noexcept {
        return !(lhs == rhs);
    }
};

}  // namespace hbrick
