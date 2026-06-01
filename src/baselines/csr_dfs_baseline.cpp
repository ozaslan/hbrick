#include "hbrick/baselines/csr_dfs_baseline.hpp"

#include "hbrick/graph/dfs.hpp"

namespace hbrick {

void CsrDfsBaseline::preprocess(const CsrGraph& graph) {
    graph_ = graph;
    status_ = BaselineStatus::Completed;
}

ReachabilityAnswer CsrDfsBaseline::query(
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return ReachabilityAnswer::Unreachable;
    }

    return Dfs::reachable(graph_, source, target, scratch);
}

}  // namespace hbrick
