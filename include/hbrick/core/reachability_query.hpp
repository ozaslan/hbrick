/**
 * @file reachability_query.hpp
 * @ingroup hbrick_core
 * @brief Typed descriptor for a source/target reachability question.
 */

#pragma once

#include "hbrick/core/vertex_id.hpp"

namespace hbrick {

/**
 * @brief A source/target pair describing one reachability question.
 * @ingroup hbrick_core
 *
 * Wraps two @ref hbrick::VertexId values and provides a convenience validity check
 * so callers can reject malformed queries before entering hot traversal paths.
 */
struct ReachabilityQuery {
    /** @brief Origin vertex of the query. Defaults to invalid. @ingroup hbrick_core */
    VertexId source = VertexId::invalid();
    /** @brief Destination vertex of the query. Defaults to invalid. @ingroup hbrick_core */
    VertexId target = VertexId::invalid();

    /** @brief Constructs an empty, invalid query. @ingroup hbrick_core */
    constexpr ReachabilityQuery() noexcept = default;

    /**
     * @brief Constructs a query from explicit source and target vertices.
     * @ingroup hbrick_core
     *
     * @param source_vertex Origin vertex.
     * @param target_vertex Destination vertex.
     */
    constexpr ReachabilityQuery(VertexId source_vertex, VertexId target_vertex) noexcept
        : source(source_vertex), target(target_vertex) {}

    /**
     * @brief Returns whether both endpoints are valid vertex identifiers.
     * @ingroup hbrick_core
     *
     * @return @c true when @ref source and @ref target are both valid.
     */
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return source.isValid() && target.isValid();
    }
};

}  // namespace hbrick
