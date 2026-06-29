#include "hbrick/graph/csr_graph_builder.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace hbrick {

CsrGraphBuilder::CsrGraphBuilder(const uint32_t num_vertices) : num_vertices_(num_vertices) {}

void CsrGraphBuilder::addEdge(const uint32_t from, const uint32_t to) {
    validateEndpoint(from);
    validateEndpoint(to);
    edges_.push_back(Edge32{from, to});
}

void CsrGraphBuilder::addEdge(const Edge32 edge) {
    addEdge(edge.from, edge.to);
}

void CsrGraphBuilder::clear() noexcept {
    edges_.clear();
}

uint64_t CsrGraphBuilder::estimateBuiltStorageBytes() const noexcept {
    return static_cast<uint64_t>(num_vertices_ + 1U) * sizeof(uint32_t)
        + static_cast<uint64_t>(edges_.size()) * sizeof(uint32_t);
}

CsrGraph CsrGraphBuilder::build() const {
    std::vector<Edge32> sorted_edges = edges_;
    std::sort(sorted_edges.begin(), sorted_edges.end(), [](const Edge32 lhs, const Edge32 rhs) {
        if (lhs.from != rhs.from) {
            return lhs.from < rhs.from;
        }
        return lhs.to < rhs.to;
    });
    sorted_edges.erase(
        std::unique(
            sorted_edges.begin(),
            sorted_edges.end(),
            [](const Edge32 lhs, const Edge32 rhs) noexcept {
                return lhs.from == rhs.from && lhs.to == rhs.to;
            }
        ),
        sorted_edges.end()
    );

    std::vector<uint32_t> row_ptrs(static_cast<std::size_t>(num_vertices_) + 1U, 0U);
    for (const Edge32& edge : sorted_edges) {
        ++row_ptrs[static_cast<std::size_t>(edge.from) + 1U];
    }

    for (std::size_t vertex = 1; vertex < row_ptrs.size(); ++vertex) {
        row_ptrs[vertex] += row_ptrs[vertex - 1U];
    }

    std::vector<uint32_t> col_indices(sorted_edges.size());
    std::vector<uint32_t> next_write = row_ptrs;
    for (const Edge32& edge : sorted_edges) {
        const std::size_t write_index = static_cast<std::size_t>(next_write[edge.from]);
        col_indices[write_index] = edge.to;
        ++next_write[edge.from];
    }

    return CsrGraph{num_vertices_, std::move(row_ptrs), std::move(col_indices)};
}

void CsrGraphBuilder::validateEndpoint(const uint32_t vertex) const {
    if (vertex >= num_vertices_) {
        throw std::out_of_range("CsrGraphBuilder edge endpoint out of range");
    }
}

}  // namespace hbrick
