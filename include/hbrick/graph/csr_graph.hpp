/**
 * @file csr_graph.hpp
 * @ingroup hbrick_graph
 * @brief Immutable directed graph in compressed sparse row (CSR) format.
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/graph/edge32.hpp"

namespace hbrick {

class CsrGraphBuilder;

/**
 * @brief Read-only directed graph stored in CSR form.
 * @ingroup hbrick_graph
 *
 * Outgoing adjacency lists are exposed as @c std::span views for cache-friendly,
 * allocation-free traversal on hot query paths. Instances are constructed only
 * through @ref hbrick::CsrGraphBuilder::build.
 */
class CsrGraph {
public:
    /** @brief Constructs an empty graph with zero vertices and edges. @ingroup hbrick_graph */
    CsrGraph() = default;

    /** @brief Returns the number of vertices (highest index plus one). @return The number of vertices (highest index plus one). @ingroup hbrick_graph */
    [[nodiscard]] uint32_t numVertices() const noexcept { return num_vertices_; }
    /** @brief Returns the total number of directed edges. @return The total number of directed edges. @ingroup hbrick_graph */
    [[nodiscard]] uint64_t numEdges() const noexcept { return col_indices_.size(); }

    /**
     * @brief Returns outgoing neighbors of @p vertex as a contiguous span.
     * @ingroup hbrick_graph
     *
     * Hot-path API: performs no heap allocation.
     *
     * @param vertex Source vertex index.
     * @return Span of destination vertex indices for edges leaving @p vertex.
     */
    [[nodiscard]] std::span<const uint32_t> outNeighbors(uint32_t vertex) const noexcept;

    /**
     * @brief Materializes the full edge list of the graph.
     * @ingroup hbrick_graph
     *
     * Allocates a new vector; intended for tests, debugging, and export.
     *
     * @return All directed edges in insertion order from construction.
     */
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
