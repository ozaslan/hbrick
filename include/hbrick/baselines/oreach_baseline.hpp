/**
 * @file oreach_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief O'Reach reachability baseline: topological orders and supporting-vertex pruning.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

/**
 * @brief Parameters for @ref hbrick::OreachBaseline preprocessing.
 * @ingroup hbrick_baselines
 */
struct OreachBaselineParams {
    /** @brief Random seed for tie-breaking and support sampling. @ingroup hbrick_baselines */
    uint64_t seed = 0x4F5245414348ULL;
    /** @brief Sampling factor l for supporting vertices. @ingroup hbrick_baselines */
    double l = 0.02;
    /** @brief Use pruned bidirectional BFS fallback when observations do not settle a query. @ingroup hbrick_baselines */
    bool pruning_bibfs = true;
    /** @brief Use pruned DFS fallback when observations do not settle a query. @ingroup hbrick_baselines */
    bool pruning_dfs = false;
};

/**
 * @brief Detailed outcome of an @ref OreachBaseline query.
 * @ingroup hbrick_baselines
 */
struct OreachQueryOutcome {
    /** @brief Reachability answer. @ingroup hbrick_baselines */
    ReachabilityAnswer answer = ReachabilityAnswer::Unreachable;
    /** @brief @c true when topological observations or bitsets settled the query without search fallback. @ingroup hbrick_baselines */
    bool settled_by_observation = false;
};

struct OreachState;

/**
 * @brief O'Reach reachability index: topological orders, support bitsets, and pruned search.
 * @ingroup hbrick_baselines
 *
 * Hanauer, Schulz, and Trummer (SEA 2021 / ACM JEA 2022).
 * Contracts input graphs to an SCC condensation DAG, computes topological orders,
 * forward/backward topological levels, and supporting-vertex reachability bitsets.
 * Settles compatible or incompatible pairs in O(1) time and falls back to pruned
 * bidirectional BFS for unsettled queries.
 */
class OreachBaseline {
public:
    OreachBaseline();
    ~OreachBaseline();

    OreachBaseline(const OreachBaseline&) = delete;
    OreachBaseline& operator=(const OreachBaseline&) = delete;

    OreachBaseline(OreachBaseline&&) noexcept;
    OreachBaseline& operator=(OreachBaseline&&) noexcept;

    /**
     * @brief Builds O'Reach index on the condensation DAG when memory allows.
     * @ingroup hbrick_baselines
     *
     * @param graph Input directed graph.
     * @param params Configuration parameters.
     * @param max_memory_bytes Upper bound on resident index bytes.
     */
    void preprocess(
        const CsrGraph& graph,
        const OreachBaselineParams& params,
        uint64_t max_memory_bytes
    );

    /**
     * @brief Answers reachability using O'Reach observation pruning and fallback search.
     * @ingroup hbrick_baselines
     *
     * @param source Source vertex index.
     * @param target Target vertex index.
     * @param scratch Reusable traversal workspace.
     * @return Exact reachability result.
     */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) const noexcept;

    /**
     * @brief Answers reachability and reports whether observations alone settled the query.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] OreachQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) const noexcept;

    /**
     * @brief Conservative label-storage estimate using @p num_vertices components.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] static uint64_t estimateIndexBytes(
        uint32_t num_vertices,
        uint64_t num_edges
    ) noexcept;

    /**
     * @brief Query-resident auxiliary bytes: component map, condensation graph, NodeInfo labels.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    uint32_t num_vertices_ = 0U;
    mutable std::unique_ptr<OreachState> state_;
};

}  // namespace hbrick
