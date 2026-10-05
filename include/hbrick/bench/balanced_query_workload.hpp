/**
 * @file balanced_query_workload.hpp
 * @ingroup hbrick_bench
 * @brief Frozen distance- and polarity-stratified reachability query pairs.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hbrick/bench/reachability_benchmark.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/passable_vertex_map.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace hbrick {

/** @brief Manhattan distance class independent of tile side. @ingroup hbrick_bench */
enum class QueryRangeClass : uint8_t {
    Local = 0,
    Medium = 1,
    Global = 2
};

/** @brief Six-bucket balanced workload specification. @ingroup hbrick_bench */
struct BalancedWorkloadSpec {
    /** @brief Total pairs across all six buckets. @ingroup hbrick_bench */
    uint32_t query_count = 10000U;
    /** @brief RNG seed for source/target sampling. @ingroup hbrick_bench */
    uint64_t seed = 0xBEEFCAFEULL;
    /** @brief Local bin is @c d < local_fraction * D. @ingroup hbrick_bench */
    double local_fraction = 0.1;
    /** @brief Medium bin is @c local_fraction * D <= d < medium_fraction * D. @ingroup hbrick_bench */
    double medium_fraction = 0.4;
    /**
     * @brief When @c true, @ref BalancedWorkload::complete is false unless every
     *        bucket reaches its requested count.
     * @ingroup hbrick_bench
     */
    bool fail_if_shortfall = true;
};

/** @brief Positive-only / negative-only query counts. @ingroup hbrick_bench */
struct PolaritySplitWorkloadSpec {
    /** @brief Reachable (s, t) pairs to sample. @ingroup hbrick_bench */
    uint32_t positive_count = 2048U;
    /** @brief Unreachable (s, t) pairs to sample. @ingroup hbrick_bench */
    uint32_t negative_count = 2048U;
    /** @brief RNG seed for source/target sampling. @ingroup hbrick_bench */
    uint64_t seed = 0xBEEFCAFEULL;
    /**
     * @brief When @c true, @ref PolaritySplitWorkload::complete is false unless
     *        both polarities reach their requested counts.
     * @ingroup hbrick_bench
     */
    bool fail_if_shortfall = true;
};

/** @brief Generated or loaded six-bucket workload. @ingroup hbrick_bench */
struct BalancedWorkload {
    std::vector<ReachabilityQueryPair> pairs;
    uint32_t requested_per_bucket[6]{};
    uint32_t filled_per_bucket[6]{};
    uint32_t manhattan_diameter = 0U;
    bool complete = true;
    uint64_t pair_list_hash = 0U;
};

/** @brief Frozen 50/50 polarity workload. @ingroup hbrick_bench */
struct PolaritySplitWorkload {
    std::vector<ReachabilityQueryPair> pairs;
    uint32_t requested_positive = 0U;
    uint32_t requested_negative = 0U;
    uint32_t filled_positive = 0U;
    uint32_t filled_negative = 0U;
    bool complete = true;
    uint64_t pair_list_hash = 0U;
};

/**
 * @brief Builds a frozen six-bucket workload of passable grid-id pairs.
 * @ingroup hbrick_bench
 *
 * Distance uses map Manhattan diameter @c D = (W-1)+(H-1). Polarity uses BFS
 * on @p compact. Leftover @c query_count % 6 pairs are given round-robin to
 * the first buckets.
 */
[[nodiscard]] BalancedWorkload generateBalancedWorkload(
    const MazeLayout& layout,
    const CsrGraph& compact,
    const PassableVertexMap& map,
    const BalancedWorkloadSpec& spec
);

/**
 * @brief Samples an equal-sized reachable and unreachable query set via BFS.
 * @ingroup hbrick_bench
 *
 * Stored order is interleaved (positive, negative, ...) so warmup mixes
 * polarity. Timed queries regroup by labeled polarity.
 */
[[nodiscard]] PolaritySplitWorkload generatePolaritySplitWorkload(
    const CsrGraph& compact,
    const PassableVertexMap& map,
    const PolaritySplitWorkloadSpec& spec
);

/** @brief Writes @p workload as CSV (@c source,target,range,polarity). @ingroup hbrick_bench */
[[nodiscard]] bool writeBalancedWorkloadCsv(
    const std::filesystem::path& path,
    const BalancedWorkload& workload,
    std::string& error_message
);

/** @brief Loads a CSV produced by @ref writeBalancedWorkloadCsv. @ingroup hbrick_bench */
[[nodiscard]] bool readBalancedWorkloadCsv(
    const std::filesystem::path& path,
    BalancedWorkload& workload,
    std::string& error_message
);

/** @brief Stable label for @p range. @ingroup hbrick_bench */
[[nodiscard]] const char* queryRangeClassLabel(QueryRangeClass range) noexcept;

}  // namespace hbrick
