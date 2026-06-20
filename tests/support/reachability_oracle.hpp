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
#include "hbrick/graph/scc_decomposition.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/tile_size.hpp"
#include "test_limits.hpp"

namespace hbrick::test_support {

/**
 * @brief Reference BFS reachability used as the ground truth in tests.
 * @ingroup hbrick_test_support
 */
[[nodiscard]] ReachabilityAnswer bfsReference(
    const CsrGraph& graph,
    uint32_t source,
    uint32_t target,
    GraphSearchScratch& scratch
);

[[nodiscard]] CsrGraph buildGridGraph(
    const MazeLayout& grid,
    GridEdgeConversionMode mode,
    RandomAsymmetricParams params = {}
);

struct BaselineOracleResult {
    std::string name;
    BaselineStatus status = BaselineStatus::NotRun;
    uint32_t mismatches = 0U;
};

[[nodiscard]] std::vector<BaselineOracleResult> runAllBaselinesAgainstBfs(
    const CsrGraph& graph,
    uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max()
);

[[nodiscard]] uint32_t countMismatchesAgainstBfs(
    const CsrGraph& graph,
    const std::function<ReachabilityAnswer(uint32_t, uint32_t)>& query
);

void expectAllBaselinesMatchBfs(
    const CsrGraph& graph,
    const std::string& context,
    uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max()
);

void expectSearchBaselinesMatchBfs(
    const CsrGraph& graph,
    const std::string& context
);

void expectBrickSearchMatchesBfs(
    const MazeLayout& layout,
    const CsrGraph& graph,
    TileSize tile_size,
    const std::string& context,
    uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max()
);

[[nodiscard]] bool mutuallyReachableViaBidirectionalBfs(
    const CsrGraph& graph,
    uint32_t left,
    uint32_t right,
    GraphSearchScratch& left_to_right_scratch,
    GraphSearchScratch& right_to_left_scratch
);

void expectSccPartitionMatchesBidirectionalBfs(
    const CsrGraph& graph,
    const std::string& context
);

/**
 * @brief Returns how many disjoint pair slices cover all V² ordered pairs.
 * @ingroup hbrick_test_support
 *
 * Returns @c 1 when @p num_vertices is at most @ref kFullAllPairsVertexLimit. Otherwise
 * returns @c ceil(V² / kMaxPairsPerSlice).
 */
[[nodiscard]] uint32_t computeReachabilityPairSliceCount(uint32_t num_vertices);

/**
 * @brief Verifies SCC labels and all baseline reachability slices for @p graph.
 * @ingroup hbrick_test_support
 *
 * Runs SCC partition once, then every deterministic pair slice in one test case.
 */
void expectReachabilityOracleAllSlices(
    const CsrGraph& graph,
    const std::string& context,
    GridEdgeConversionMode mode = GridEdgeConversionMode::RandomAsymmetric,
    uint32_t full_all_pairs_vertex_limit = kFullAllPairsVertexLimit,
    uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max()
);

/**
 * @brief Counts ordered pairs assigned to @p slice_id when splitting V² pairs into @p slice_count slices.
 * @ingroup hbrick_test_support
 *
 * Pairs are enumerated in row-major order (@c source major, @c target minor). Pair index
 * @c p = source * V + target is included when @c p % slice_count == slice_id.
 */
[[nodiscard]] uint64_t reachabilityPairCountInSlice(
    uint32_t num_vertices,
    uint32_t slice_id,
    uint32_t slice_count
);

void expectSearchBaselinesMatchBfsOnSlice(
    const CsrGraph& graph,
    const std::string& context,
    uint32_t slice_id,
    uint32_t slice_count
);

void expectAllBaselinesMatchBfsOnSlice(
    const CsrGraph& graph,
    const std::string& context,
    uint32_t slice_id,
    uint32_t slice_count,
    uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max()
);

/**
 * @brief Verifies SCC labels and baseline reachability for one deterministic pair slice.
 * @ingroup hbrick_test_support
 *
 * Runs @ref expectSccPartitionMatchesBidirectionalBfs on @p slice_id @c 0 only. When
 * @p num_vertices <= @p full_all_pairs_vertex_limit, tests all V² pairs in slice 0 via all
 * baselines. Otherwise tests the search baselines on the pairs belonging to @p slice_id.
 */
void expectReachabilityOracleSlice(
    const CsrGraph& graph,
    const std::string& context,
    uint32_t slice_id,
    GridEdgeConversionMode mode = GridEdgeConversionMode::RandomAsymmetric,
    uint32_t full_all_pairs_vertex_limit = kFullAllPairsVertexLimit,
    uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max()
);

/** @brief Runs @ref expectReachabilityOracleAllSlices. @ingroup hbrick_test_support */
void expectExhaustiveReachabilityOracle(
    const CsrGraph& graph,
    const std::string& context,
    GridEdgeConversionMode mode = GridEdgeConversionMode::RandomAsymmetric,
    uint32_t full_all_pairs_vertex_limit = kFullAllPairsVertexLimit,
    uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max()
);

}  // namespace hbrick::test_support
