#pragma once

#include <cstdint>

#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

class DagReachability {
public:
    [[nodiscard]] static ReachabilityAnswer reachable(
        const CsrGraph& dag,
        uint32_t source_component,
        uint32_t target_component,
        GraphSearchScratch& scratch
    ) noexcept;
};

}  // namespace hbrick
