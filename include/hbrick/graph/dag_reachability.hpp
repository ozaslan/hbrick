/**
 * @file dag_reachability.hpp
 * @ingroup hbrick_graph
 * @brief Reachability queries on directed acyclic graphs.
 */

#pragma once

#include <cstdint>

#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

/**
 * @brief Reachability helper specialized for DAG inputs.
 * @ingroup hbrick_graph
 *
 * Typically used on the condensation DAG produced by @ref hbrick::CondensationGraph.
 * Performs graph search without the cycle-handling overhead required on general
 * directed graphs.
 */
class DagReachability {
public:
    /**
     * @brief Returns whether @p target_component is reachable from @p source_component.
     * @ingroup hbrick_graph
     *
     * @param dag Directed acyclic graph whose vertices are component ids.
     * @param source_component Source component id.
     * @param target_component Target component id.
     * @param scratch Reusable traversal workspace.
     * @return @ref hbrick::ReachabilityAnswer::Reachable when a directed path exists.
     */
    [[nodiscard]] static ReachabilityAnswer reachable(
        const CsrGraph& dag,
        uint32_t source_component,
        uint32_t target_component,
        GraphSearchScratch& scratch
    ) noexcept;
};

}  // namespace hbrick
