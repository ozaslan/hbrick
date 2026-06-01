/**
 * @file vertex_id.hpp
 * @ingroup hbrick_core
 * @brief Strongly typed vertex identifiers for graph and grid APIs.
 */

#pragma once

#include <cstdint>
#include <limits>

namespace hbrick {

/**
 * @brief Sentinel value representing an invalid vertex identifier.
 * @ingroup hbrick_core
 *
 * Equal to @c std::numeric_limits<uint32_t>::max() and used by @ref hbrick::VertexId
 * to distinguish unset endpoints from valid graph vertices.
 */
inline constexpr uint32_t kInvalidVertexId = std::numeric_limits<uint32_t>::max();

/**
 * @brief Lightweight wrapper around a raw graph vertex index.
 * @ingroup hbrick_core
 *
 * Provides a typed alternative to bare @c uint32_t vertex indices and an
 * explicit invalid state for optional endpoints in query descriptors.
 */
struct VertexId {
    /** @brief Raw zero-based vertex index, or @ref hbrick::kInvalidVertexId when unset. @ingroup hbrick_core */
    uint32_t value = kInvalidVertexId;

    /** @brief Default-constructs an invalid identifier. @ingroup hbrick_core */
    constexpr VertexId() noexcept = default;

    /**
     * @brief Constructs a vertex identifier from a raw index.
     * @ingroup hbrick_core
     *
     * @param id Zero-based vertex index. Callers must ensure the index is
     *           in range for the graph being queried.
     */
    constexpr explicit VertexId(uint32_t id) noexcept : value(id) {}

    /**
     * @brief Returns whether this identifier represents a valid vertex.
     * @ingroup hbrick_core
     *
     * @return @c false when @ref value equals @ref hbrick::kInvalidVertexId.
     */
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return value != kInvalidVertexId;
    }

    /**
     * @brief Returns the canonical invalid vertex identifier.
     * @ingroup hbrick_core
     *
     * @return A @ref hbrick::VertexId whose @ref value is @ref hbrick::kInvalidVertexId.
     */
    [[nodiscard]] static constexpr VertexId invalid() noexcept {
        return VertexId{kInvalidVertexId};
    }

    /** @brief Compares wrapped raw indices for equality. @ingroup hbrick_core */
    friend constexpr bool operator==(VertexId lhs, VertexId rhs) noexcept {
        return lhs.value == rhs.value;
    }

    /** @brief Compares wrapped raw indices for inequality. @ingroup hbrick_core */
    friend constexpr bool operator!=(VertexId lhs, VertexId rhs) noexcept {
        return !(lhs == rhs);
    }
};

}  // namespace hbrick
