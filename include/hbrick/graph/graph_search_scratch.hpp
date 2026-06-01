/**
 * @file graph_search_scratch.hpp
 * @ingroup hbrick_graph
 * @brief Reusable scratch buffers for BFS, DFS, and SCC algorithms.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hbrick {

class GraphSearchScratchOverflowTest;

/**
 * @brief Preallocated workspace reused across graph traversals.
 * @ingroup hbrick_graph
 *
 * Separates allocation from hot traversal paths: callers size scratch once
 * per graph, then pass the same instance to repeated @ref hbrick::Bfs, @ref hbrick::Dfs, or
 * @ref hbrick::SccDecomposition calls. Visited marking uses generation stamps via
 * @ref nextMark rather than clearing the full visited array each query.
 */
class GraphSearchScratch {
public:
    /** @brief Constructs scratch with zero capacity. @ingroup hbrick_graph */
    GraphSearchScratch() = default;

    /**
     * @brief Constructs scratch sized for a graph with @p num_vertices vertices.
     * @ingroup hbrick_graph
     *
     * @param num_vertices Vertex count used to size internal buffers.
     */
    explicit GraphSearchScratch(uint32_t num_vertices);

    /**
     * @brief Resizes internal buffers for a graph with @p num_vertices vertices.
     * @ingroup hbrick_graph
     *
     * Resets the visit-mark generation counter.
     *
     * @param num_vertices New vertex count.
     */
    void resetForGraph(uint32_t num_vertices);

    /**
     * @brief Returns the next visitation stamp for timestamp-based marking.
     * @ingroup hbrick_graph
     *
     * Increments an internal counter. When the counter would overflow
     * @c uint32_t, the visited array is zeroed and counting restarts at one.
     *
     * @return Stamp value to store in @ref visitedMark() for the current pass.
     */
    uint32_t nextMark();

    /** @brief Returns the BFS queue buffer. @return The BFS queue buffer. @ingroup hbrick_graph */
    std::vector<uint32_t>& queue() noexcept { return queue_; }
    /** @brief Returns the DFS stack buffer. @return The DFS stack buffer. @ingroup hbrick_graph */
    std::vector<uint32_t>& stack() noexcept { return stack_; }
    /** @brief Returns the per-vertex visitation stamp array. @return The per-vertex visitation stamp array. @ingroup hbrick_graph */
    std::vector<uint32_t>& visitedMark() noexcept { return visited_mark_; }

    /** @brief Returns the read-only BFS queue buffer. @return The read-only BFS queue buffer. @ingroup hbrick_graph */
    const std::vector<uint32_t>& queue() const noexcept { return queue_; }
    /** @brief Returns the read-only DFS stack buffer. @return The read-only DFS stack buffer. @ingroup hbrick_graph */
    const std::vector<uint32_t>& stack() const noexcept { return stack_; }
    /** @brief Returns the read-only visitation stamp array. @return The read-only visitation stamp array. @ingroup hbrick_graph */
    const std::vector<uint32_t>& visitedMark() const noexcept { return visited_mark_; }

    /**
     * @brief Estimates total heap memory owned by scratch buffers.
     * @ingroup hbrick_graph
     *
     * @return Approximate byte count including vector capacity.
     */
    [[nodiscard]] size_t memoryBytes() const noexcept;

private:
    friend class GraphSearchScratchOverflowTest;

    std::vector<uint32_t> queue_;
    std::vector<uint32_t> stack_;
    std::vector<uint32_t> visited_mark_;

    uint32_t current_mark_ = 1;
};

}  // namespace hbrick
