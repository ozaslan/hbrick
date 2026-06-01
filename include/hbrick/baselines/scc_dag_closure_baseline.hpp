#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/scc_decomposition.hpp"

namespace hbrick {

class SccDagClosureBaseline {
public:
    void preprocess(
        const CsrGraph& graph,
        GraphSearchScratch& scratch,
        uint64_t max_memory_bytes
    );

    [[nodiscard]] ReachabilityAnswer query(uint32_t source, uint32_t target) const noexcept;

    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    SccDecomposition decomposition_;
    BitMatrix closure_;
};

}  // namespace hbrick
