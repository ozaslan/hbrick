/**
 * @file edge32.hpp
 * @ingroup hbrick_graph
 * @brief Lightweight directed edge with 32-bit endpoint indices.
 */

#pragma once

#include <cstdint>

namespace hbrick {

/**
 * @brief Directed edge between two vertices identified by @c uint32_t indices.
 * @ingroup hbrick_graph
 *
 * Used during CSR construction and when exporting edge lists from graph
 * objects. Both endpoints are zero-based vertex indices.
 */
struct Edge32 {
    /** @brief Source (tail) vertex index. @ingroup hbrick_graph */
    uint32_t from = 0;
    /** @brief Destination (head) vertex index. @ingroup hbrick_graph */
    uint32_t to = 0;

    /** @brief Default-constructs a zero-to-zero edge. @ingroup hbrick_graph */
    constexpr Edge32() noexcept = default;

    /**
     * @brief Constructs an edge from explicit endpoint indices.
     * @ingroup hbrick_graph
     *
     * @param from_vertex Source vertex index.
     * @param to_vertex Destination vertex index.
     */
    constexpr Edge32(uint32_t from_vertex, uint32_t to_vertex) noexcept
        : from(from_vertex), to(to_vertex) {}

    /** @brief Compares both endpoints for equality. @ingroup hbrick_graph */
    friend constexpr bool operator==(Edge32 lhs, Edge32 rhs) noexcept {
        return lhs.from == rhs.from && lhs.to == rhs.to;
    }

    /** @brief Compares both endpoints for inequality. @ingroup hbrick_graph */
    friend constexpr bool operator!=(Edge32 lhs, Edge32 rhs) noexcept {
        return !(lhs == rhs);
    }
};

}  // namespace hbrick
