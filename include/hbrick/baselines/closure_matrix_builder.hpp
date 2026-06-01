#pragma once

#include <cstdint>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/graph/csr_graph.hpp"

namespace hbrick {

class ClosureMatrixBuilder {
public:
    [[nodiscard]] static uint64_t estimateReflexiveAdjacencyBytes(uint32_t num_vertices) noexcept;

    [[nodiscard]] static bool canAllocateReflexiveAdjacency(
        uint32_t num_vertices,
        uint64_t max_memory_bytes
    ) noexcept;

    [[nodiscard]] static BitMatrix buildReflexiveAdjacencyOrThrow(
        const CsrGraph& graph,
        uint64_t max_memory_bytes
    );
};

}  // namespace hbrick
