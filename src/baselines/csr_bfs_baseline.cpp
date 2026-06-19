#include "hbrick/baselines/csr_bfs_baseline.hpp"

#include "hbrick/graph/bfs.hpp"

namespace hbrick {

void CsrBfsBaseline::preprocess(const CsrGraph& graph) {
    graph_ = &graph;
    status_ = BaselineStatus::Completed;
}

const CsrGraph& CsrBfsBaseline::graph() const noexcept {
    static const CsrGraph kEmpty{};
    return graph_ != nullptr ? *graph_ : kEmpty;
}

ReachabilityAnswer CsrBfsBaseline::query(
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) const noexcept {
    if (status_ != BaselineStatus::Completed || graph_ == nullptr) {
        return ReachabilityAnswer::Unreachable;
    }

    return Bfs::reachable(*graph_, source, target, scratch);
}

}  // namespace hbrick
