/**
 * @file kleene_squaring_bounds.hpp
 * @ingroup hbrick_graph
 * @brief Truncated Kleene squaring step counts for reflexive boolean closure.
 */

#pragma once

#include <cstdint>

namespace hbrick {

class BitMatrix;
class CsrGraph;
class GraphSearchScratch;

/**
 * @brief Diagnostic inputs and the chosen truncated Kleene squaring budget.
 * @ingroup hbrick_graph
 */
struct KleeneSquaringBoundStats {
    /** @brief Largest undirected component in the reflexive relation. @ingroup hbrick_graph */
    uint32_t largest_undirected_component = 0U;
    /** @brief Number of directed strongly connected components. @ingroup hbrick_graph */
    uint32_t num_strongly_connected_components = 0U;
    /** @brief Size of the largest directed strongly connected component. @ingroup hbrick_graph */
    uint32_t largest_strongly_connected_component = 0U;
    /** @brief Truncated squaring rounds passed to @ref BooleanClosure. @ingroup hbrick_graph */
    uint32_t squaring_count = 0U;
};

/**
 * @brief Computes truncated Kleene squaring rounds for @p graph.
 * @ingroup hbrick_graph
 *
 * Uses the minimum of:
 * - @c ceil(log2(n_max)) from the largest undirected component, and
 * - @c ceil(log2(C + S_max)) where @c C is the directed SCC count and
 *   @c S_max is the largest directed SCC size.
 *
 * Early fixpoint termination inside Kleene squaring may stop sooner.
 */
[[nodiscard]] KleeneSquaringBoundStats kleeneSquaringBoundStatsForCsrGraph(
    const CsrGraph& graph,
    GraphSearchScratch& scratch
) noexcept;

/**
 * @brief Computes truncated Kleene squaring rounds for a reflexive @c I|A bit matrix.
 * @ingroup hbrick_graph
 */
[[nodiscard]] KleeneSquaringBoundStats kleeneSquaringBoundStatsForReflexiveAdjacency(
    const BitMatrix& reflexive_adjacency,
    GraphSearchScratch& scratch
) noexcept;

/** @brief Returns @ref KleeneSquaringBoundStats::squaring_count for @p graph. @ingroup hbrick_graph */
[[nodiscard]] uint32_t kleeneSquaringCountForCsrGraph(
    const CsrGraph& graph,
    GraphSearchScratch& scratch
) noexcept;

/**
 * @brief Returns @ref KleeneSquaringBoundStats::squaring_count for @p reflexive_adjacency.
 * @ingroup hbrick_graph
 */
[[nodiscard]] uint32_t kleeneSquaringCountForReflexiveAdjacency(
    const BitMatrix& reflexive_adjacency,
    GraphSearchScratch& scratch
) noexcept;

}  // namespace hbrick
