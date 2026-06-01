#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"

namespace hbrick {

class FullClosureBaseline {
public:
    void preprocess(const CsrGraph& graph, uint64_t max_memory_bytes);

    [[nodiscard]] ReachabilityAnswer query(uint32_t source, uint32_t target) const noexcept;

    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }
    [[nodiscard]] uint32_t numVertices() const noexcept { return num_vertices_; }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    uint32_t num_vertices_ = 0;
    BitMatrix closure_;
};

}  // namespace hbrick
