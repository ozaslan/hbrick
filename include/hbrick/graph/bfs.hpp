#pragma once

#include <cstdint>

#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

class Bfs {
public:
    [[nodiscard]] static ReachabilityAnswer reachable(
        const CsrGraph& graph,
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) noexcept;
};

}  // namespace hbrick
