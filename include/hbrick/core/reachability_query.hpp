#pragma once

#include "hbrick/core/vertex_id.hpp"

namespace hbrick {

struct ReachabilityQuery {
    VertexId source = VertexId::invalid();
    VertexId target = VertexId::invalid();

    constexpr ReachabilityQuery() noexcept = default;

    constexpr ReachabilityQuery(VertexId source_vertex, VertexId target_vertex) noexcept
        : source(source_vertex), target(target_vertex) {}

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return source.isValid() && target.isValid();
    }
};

}  // namespace hbrick
