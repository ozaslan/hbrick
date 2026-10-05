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

uint64_t CsrGraph::estimateStorageBytes() const noexcept {
    return static_cast<uint64_t>(row_ptrs_.capacity()) * sizeof(uint32_t)
        + static_cast<uint64_t>(col_indices_.capacity()) * sizeof(uint32_t);
}

CsrGraph CsrGraph::transpose() const {
    if (num_vertices_ == 0U) {
        return CsrGraph{0U, {0U}, {}};
    }

    std::vector<uint32_t> trans_row_ptrs(static_cast<std::size_t>(num_vertices_) + 1U, 0U);
    for (const uint32_t to : col_indices_) {
        if (to < num_vertices_) {
            ++trans_row_ptrs[static_cast<std::size_t>(to) + 1U];
        }
    }

    for (std::size_t i = 1U; i < trans_row_ptrs.size(); ++i) {
        trans_row_ptrs[i] += trans_row_ptrs[i - 1U];
    }

    std::vector<uint32_t> trans_col_indices(col_indices_.size());
    std::vector<uint32_t> next_pos = trans_row_ptrs;

    for (uint32_t from = 0U; from < num_vertices_; ++from) {
        for (const uint32_t to : outNeighbors(from)) {
            const std::size_t slot = static_cast<std::size_t>(next_pos[to]);
            trans_col_indices[slot] = from;
            ++next_pos[to];
        }
    }

    return CsrGraph{num_vertices_, std::move(trans_row_ptrs), std::move(trans_col_indices)};
}

}  // namespace hbrick
