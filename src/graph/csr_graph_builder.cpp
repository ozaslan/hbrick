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

void CsrGraphBuilder::clear() noexcept {
    edges_.clear();
}

uint64_t CsrGraphBuilder::estimateBuiltStorageBytes() const noexcept {
    return static_cast<uint64_t>(num_vertices_ + 1U) * sizeof(uint32_t)
        + static_cast<uint64_t>(edges_.size()) * sizeof(uint32_t);
}

uint64_t CsrGraphBuilder::estimatePendingStorageBytes() const noexcept {
    return static_cast<uint64_t>(edges_.capacity()) * sizeof(Edge32);
}

CsrGraph CsrGraphBuilder::buildFromEdges(std::vector<Edge32>& sorted_edges) const {
    if (num_vertices_ == 0U) {
        return CsrGraph{0U, {0U}, {}};
    }
    if (sorted_edges.empty()) {
        return CsrGraph{
            num_vertices_,
            std::vector<uint32_t>(static_cast<std::size_t>(num_vertices_) + 1U, 0U),
            {}
        };
    }

    std::vector<uint32_t> offsets(static_cast<std::size_t>(num_vertices_) + 1U, 0U);
    for (const Edge32& edge : sorted_edges) {
        ++offsets[static_cast<std::size_t>(edge.from) + 1U];
    }

    for (std::size_t v = 1U; v <= num_vertices_; ++v) {
        offsets[v] += offsets[v - 1U];
    }

    std::vector<uint32_t> cursor = offsets;
    std::vector<uint32_t> col_indices(sorted_edges.size());
    for (const Edge32& edge : sorted_edges) {
        col_indices[cursor[edge.from]++] = edge.to;
    }

    std::vector<uint32_t> row_ptrs(static_cast<std::size_t>(num_vertices_) + 1U, 0U);
    std::size_t write_pos = 0U;
    for (uint32_t v = 0U; v < num_vertices_; ++v) {
        const std::size_t start = offsets[v];
        const std::size_t end = offsets[v + 1U];
        if (start < end) {
            uint32_t* const n_begin = col_indices.data() + start;
            uint32_t* const n_end = col_indices.data() + end;
            std::sort(n_begin, n_end);
            uint32_t* const u_end = std::unique(n_begin, n_end);
            const std::size_t unique_count = static_cast<std::size_t>(u_end - n_begin);
            if (write_pos != start) {
                std::copy_n(n_begin, unique_count, col_indices.data() + write_pos);
            }
            write_pos += unique_count;
        }
        row_ptrs[static_cast<std::size_t>(v) + 1U] = static_cast<uint32_t>(write_pos);
    }
    col_indices.resize(write_pos);

    return CsrGraph{num_vertices_, std::move(row_ptrs), std::move(col_indices)};
}

CsrGraph CsrGraphBuilder::build() const & {
    std::vector<Edge32> sorted_edges = edges_;
    return buildFromEdges(sorted_edges);
}

CsrGraph CsrGraphBuilder::build() && {
    return buildFromEdges(edges_);
}

CsrGraph CsrGraphBuilder::buildDestructive() {
    return buildFromEdges(edges_);
}

void CsrGraphBuilder::validateEndpoint(const uint32_t vertex) const {
    if (vertex >= num_vertices_) {
        throw std::out_of_range("CsrGraphBuilder edge endpoint out of range");
    }
}

}  // namespace hbrick
