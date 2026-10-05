#include "hbrick/graph/dag_reachability.hpp"

#include "hbrick/graph/bfs.hpp"

namespace hbrick {

ReachabilityAnswer DagReachability::reachable(
    const CsrGraph& dag,
    const uint32_t source_component,
    const uint32_t target_component,
    GraphSearchScratch& scratch
) noexcept {
    if (source_component == target_component) {
        return ReachabilityAnswer::Reachable;
    }

    return Bfs::reachable(dag, source_component, target_component, scratch);
}

}  // namespace hbrick
