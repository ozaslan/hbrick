#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/graph/edge32.hpp"

namespace hbrick {

class CsrGraphBuilder;

class CsrGraph {
public:
    CsrGraph() = default;

    [[nodiscard]] uint32_t numVertices() const noexcept { return num_vertices_; }
    [[nodiscard]] uint64_t numEdges() const noexcept { return col_indices_.size(); }

    [[nodiscard]] std::span<const uint32_t> outNeighbors(uint32_t vertex) const noexcept;

    [[nodiscard]] std::vector<Edge32> edges() const;

private:
    friend class CsrGraphBuilder;

    CsrGraph(
        uint32_t num_vertices,
        std::vector<uint32_t> row_ptrs,
        std::vector<uint32_t> col_indices
    );

    uint32_t num_vertices_ = 0;
    std::vector<uint32_t> row_ptrs_;
    std::vector<uint32_t> col_indices_;
};

}  // namespace hbrick
