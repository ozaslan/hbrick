#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

class CsrDfsBaseline {
public:
    void preprocess(const CsrGraph& graph);

    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) const noexcept;

    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }
    [[nodiscard]] const CsrGraph& graph() const noexcept { return graph_; }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    CsrGraph graph_;
};

}  // namespace hbrick
