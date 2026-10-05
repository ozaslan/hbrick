#include "hbrick/baselines/csr_dfs_baseline.hpp"

#include "hbrick/graph/dfs.hpp"

namespace hbrick {

void CsrDfsBaseline::preprocess(const CsrGraph& graph) {
    graph_ = &graph;
    status_ = BaselineStatus::Completed;
}

const CsrGraph& CsrDfsBaseline::graph() const noexcept {
    static const CsrGraph kEmpty{};
    return graph_ != nullptr ? *graph_ : kEmpty;
}

ReachabilityAnswer CsrDfsBaseline::query(
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) const noexcept {
    if (status_ != BaselineStatus::Completed || graph_ == nullptr) {
        return ReachabilityAnswer::Unreachable;
    }

    return Dfs::reachable(*graph_, source, target, scratch);
}

}  // namespace hbrick
