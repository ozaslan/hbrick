/**
 * @file reachability_oracle.hpp
 * @ingroup hbrick_test_support
 * @brief Correctness oracles comparing baselines against BFS reference answers.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/passable_grid.hpp"

namespace hbrick::test_support {

/**
 * @brief Reference BFS reachability used as the ground truth in tests.
 * @ingroup hbrick_test_support
 *
 * Thin wrapper around @ref hbrick::Bfs::reachable for test readability.
 *
 * @param graph Graph under test.
 * @param source Source vertex index.
 * @param target Target vertex index.
 * @param scratch Reusable traversal workspace.
 * @return BFS reachability answer.
 */
[[nodiscard]] ReachabilityAnswer bfsReference(
    const CsrGraph& graph,
    uint32_t source,
    uint32_t target,
    GraphSearchScratch& scratch
);

/**
 * @brief Builds a CSR graph from a passable grid using the given conversion mode.
 * @ingroup hbrick_test_support
 *
 * Convenience wrapper around @ref hbrick::DirectedGridGraphBuilder::build that returns
 * only the CSR representation.
 *
 * @param grid Passable cell layout.
 * @param mode Edge orientation policy.
 * @param params Randomness parameters for asymmetric conversion.
 * @return CSR graph suitable for baseline and oracle tests.
 */
[[nodiscard]] CsrGraph buildGridGraph(
    const PassableGrid& grid,
    GridEdgeConversionMode mode,
    RandomAsymmetricParams params = {}
);

/**
 * @brief Summary of one baseline run compared against BFS on all vertex pairs.
 * @ingroup hbrick_test_support
 */
struct BaselineOracleResult {
    /** @brief Human-readable baseline name. @ingroup hbrick_test_support */
    std::string name;
    /** @brief Final preprocess status reported by the baseline. @ingroup hbrick_test_support */
    BaselineStatus status = BaselineStatus::NotRun;
    /** @brief Number of (source, target) pairs where the baseline disagreed with BFS. @ingroup hbrick_test_support */
    uint32_t mismatches = 0U;
};

/**
 * @brief Runs every shipped baseline and compares all pairs against BFS.
 * @ingroup hbrick_test_support
 *
 * @param graph Graph instance shared by all baselines.
 * @param max_memory_bytes Memory budget forwarded to closure-based baselines.
 * @return Per-baseline mismatch summaries.
 */
[[nodiscard]] std::vector<BaselineOracleResult> runAllBaselinesAgainstBfs(
    const CsrGraph& graph,
    uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max()
);

/**
 * @brief Counts vertex pairs where @p query disagrees with BFS.
 * @ingroup hbrick_test_support
 *
 * @param graph Graph to enumerate.
 * @param query Callable returning a reachability answer for (source, target).
 * @return Number of mismatched ordered pairs.
 */
[[nodiscard]] uint32_t countMismatchesAgainstBfs(
    const CsrGraph& graph,
    const std::function<ReachabilityAnswer(uint32_t, uint32_t)>& query
);

/**
 * @brief Google Test helper that fails when any baseline disagrees with BFS.
 * @ingroup hbrick_test_support
 *
 * @param graph Graph instance to validate.
 * @param context String included in failure messages for scenario identification.
 * @param max_memory_bytes Memory budget forwarded to closure-based baselines.
 */
void expectAllBaselinesMatchBfs(
    const CsrGraph& graph,
    const std::string& context,
    uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max()
);

/**
 * @brief Like expectAllBaselinesMatchBfs but skips closure preprocess (CsrBfs, CsrDfs, SccDagSearch only).
 * @ingroup hbrick_test_support
 *
 * Use on bidirectional grid graphs where the passable region forms one large SCC; boolean
 * closure preprocess is O(n^3) and redundant with cycle-graph closure tests elsewhere.
 *
 * @param graph Graph instance to validate.
 * @param context String included in failure messages for scenario identification.
 */
void expectSearchBaselinesMatchBfs(
    const CsrGraph& graph,
    const std::string& context
);

}  // namespace hbrick::test_support
