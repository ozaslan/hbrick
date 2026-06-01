#pragma once

#include <cstdint>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/scc_decomposition.hpp"

namespace hbrick {

class CondensationGraph {
public:
    CondensationGraph() = default;

    [[nodiscard]] static CondensationGraph fromGraph(
        const CsrGraph& graph,
        const SccDecomposition& decomposition
    );

    [[nodiscard]] uint32_t numComponents() const noexcept { return decomposition_.numComponents(); }
    [[nodiscard]] uint32_t componentOf(uint32_t vertex) const noexcept {
        return decomposition_.componentOf(vertex);
    }

    [[nodiscard]] const CsrGraph& dag() const noexcept { return dag_; }
    [[nodiscard]] const SccDecomposition& decomposition() const noexcept { return decomposition_; }

private:
    CondensationGraph(SccDecomposition decomposition, CsrGraph dag);

    SccDecomposition decomposition_;
    CsrGraph dag_;
};

}  // namespace hbrick
