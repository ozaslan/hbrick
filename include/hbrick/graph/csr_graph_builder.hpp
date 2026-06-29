/**
 * @file csr_graph_builder.hpp
 * @ingroup hbrick_graph
 * @brief Incremental builder that produces immutable @ref hbrick::CsrGraph instances.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/edge32.hpp"

namespace hbrick {

/**
 * @brief Collects edges and compacts them into CSR storage.
 * @ingroup hbrick_graph
 *
 * Vertices are numbered @c 0 .. numVertices()-1. Duplicate edges may be
 * added; @ref build deduplicates them during compaction.
 */
class CsrGraphBuilder {
public:
    /**
     * @brief Creates a builder for a graph with a fixed vertex count.
     * @ingroup hbrick_graph
     *
     * @param num_vertices Number of vertices; indices range from @c 0 to
     *                     @c num_vertices - 1.
     */
    explicit CsrGraphBuilder(uint32_t num_vertices);

    /** @brief Returns the configured vertex count. @return The configured vertex count. @ingroup hbrick_graph */
    [[nodiscard]] uint32_t numVertices() const noexcept { return num_vertices_; }
    /** @brief Returns how many edges have been added but not yet built. @return how many edges have been added but not yet built. @ingroup hbrick_graph */
    [[nodiscard]] uint64_t pendingEdgeCount() const noexcept { return edges_.size(); }

    /**
     * @brief Appends a directed edge.
     * @ingroup hbrick_graph
     *
     * @param from Source vertex index.
     * @param to Destination vertex index.
     */
    void addEdge(uint32_t from, uint32_t to);

    /**
     * @brief Appends a directed edge from an @ref hbrick::Edge32 value.
     * @ingroup hbrick_graph
     *
     * @param edge Edge endpoints to append.
     */
    void addEdge(Edge32 edge);

    /** @brief Removes all pending edges without changing the vertex count. @ingroup hbrick_graph */
    void clear() noexcept;

    /**
     * @brief Sorts and compacts pending edges into a @ref hbrick::CsrGraph.
     * @ingroup hbrick_graph
     *
     * Duplicate edges are removed during compaction.
     *
     * @return Immutable CSR graph containing all edges added since construction
     *         or the last @ref clear.
     */
    [[nodiscard]] CsrGraph build() const;

    /**
     * @brief Upper bound on @ref CsrGraph::estimateStorageBytes for the next @ref build.
     * @ingroup hbrick_graph
     */
    [[nodiscard]] uint64_t estimateBuiltStorageBytes() const noexcept;

private:
    /** @brief Validates that @p vertex is within the configured vertex range. @ingroup hbrick_graph */
    void validateEndpoint(uint32_t vertex) const;

    uint32_t num_vertices_ = 0;
    std::vector<Edge32> edges_;
};

}  // namespace hbrick
