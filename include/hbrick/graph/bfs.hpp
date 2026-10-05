/**
 * @file bfs.hpp
 * @ingroup hbrick_graph
 * @brief Breadth-first reachability on @ref hbrick::CsrGraph.
 */

#pragma once

#include <cstdint>

#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

/**
 * @brief Static breadth-first search reachability helper.
 * @ingroup hbrick_graph
 *
 * Answers single-pair reachability queries by exploring outgoing edges layer
 * by layer. The hot path uses only preallocated scratch buffers.
 */
class Bfs {
public:
    /**
     * @brief Returns whether @p target is reachable from @p source.
     * @ingroup hbrick_graph
     *
     * @param graph Directed graph to search.
     * @param source Source vertex index.
     * @param target Target vertex index.
     * @param scratch Reusable traversal workspace sized for @p graph.
     * @return @ref hbrick::ReachabilityAnswer::Reachable when a directed path exists.
     */
    [[nodiscard]] static ReachabilityAnswer reachable(
        const CsrGraph& graph,
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) noexcept;

    /**
     * @brief Returns how many vertices are forward-reachable from @p source.
     * @ingroup hbrick_graph
     *
     * Counts @p source itself plus every vertex discovered by a full forward
     * BFS. The hot path uses only preallocated scratch buffers.
     *
     * @param graph Directed graph to search.
     * @param source Source vertex index.
     * @param scratch Reusable traversal workspace sized for @p graph.
     * @return Reachable vertex count, or @c 0 when @p source is out of range.
     */
    [[nodiscard]] static uint32_t reachableCount(
        const CsrGraph& graph,
        uint32_t source,
        GraphSearchScratch& scratch
    ) noexcept;
};

}  // namespace hbrick
