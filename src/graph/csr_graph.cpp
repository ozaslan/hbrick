#include "hbrick/graph/csr_graph.hpp"

#include <algorithm>

#include "hbrick/graph/edge32.hpp"

namespace hbrick {

CsrGraph::CsrGraph(
    const uint32_t num_vertices,
    std::vector<uint32_t> row_ptrs,
    std::vector<uint32_t> col_indices
)
    : num_vertices_(num_vertices),
      row_ptrs_(std::move(row_ptrs)),
      col_indices_(std::move(col_indices)) {}

std::span<const uint32_t> CsrGraph::outNeighbors(const uint32_t vertex) const noexcept {
    if (vertex >= num_vertices_ || row_ptrs_.size() != static_cast<std::size_t>(num_vertices_) + 1U) {
        return {};
    }

    const uint32_t begin = row_ptrs_[vertex];
    const uint32_t end = row_ptrs_[vertex + 1U];
    return {col_indices_.data() + begin, end - begin};
}

std::vector<Edge32> CsrGraph::edges() const {
    std::vector<Edge32> result;
    result.reserve(col_indices_.size());

    for (uint32_t from = 0; from < num_vertices_; ++from) {
        for (const uint32_t to : outNeighbors(from)) {
            result.push_back(Edge32{from, to});
        }
    }

    std::sort(result.begin(), result.end(), [](const Edge32 lhs, const Edge32 rhs) {
        if (lhs.from != rhs.from) {
            return lhs.from < rhs.from;
        }
        return lhs.to < rhs.to;
    });

    return result;
}

}  // namespace hbrick
