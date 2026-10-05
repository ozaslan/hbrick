/**
 * @file hbrick_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief Hierarchical H-BRICK reachability via attachment propagation and ancestor tests.
 */

#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/baselines/hbrick_query_scratch.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hbrick_index.hpp"
#include "hbrick/tile/brick_index.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/**
 * @brief Detailed outcome of a @ref HBrickBaseline query.
 * @ingroup hbrick_baselines
 */
struct HBrickQueryOutcome {
    /** @brief Reachability answer. @ingroup hbrick_baselines */
    ReachabilityAnswer answer = ReachabilityAnswer::Unreachable;
    /** @brief Same-tile local closure settled the query. @ingroup hbrick_baselines */
    bool local_hit = false;
    /** @brief An ancestor interface meet settled the query. @ingroup hbrick_baselines */
    bool ancestor_hit = false;
    /**
     * @brief Hierarchy level of a local or ancestor hit; @c UINT32_MAX otherwise.
     * @ingroup hbrick_baselines
     */
    uint32_t resolved_level = 0U;
    /** @brief Common-ancestor levels examined before the answer. @ingroup hbrick_baselines */
    uint32_t ancestors_checked = 0U;
    /** @brief Flat port-graph BFS was used because the hierarchy is truncated. @ingroup hbrick_baselines */
    bool used_flat_fallback = false;
};

/**
 * @brief H-BRICK baseline: hierarchical boolean propagation with ancestor meet tests.
 * @ingroup hbrick_baselines
 */
class HBrickBaseline {
public:
    /**
     * @brief Builds the H-BRICK index for @p graph.
     * @ingroup hbrick_baselines
     */
    void preprocess(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        HBrickConfig config
    );

    /**
     * @brief Binds a pre-built @ref HBrickIndex for query-only use (no preprocess rebuild).
     * @ingroup hbrick_baselines
     *
     * Replaces any prior index held by this baseline and prepares query scratch from @p index.
     */
    void adoptPrebuiltIndex(HBrickIndex index, const DirectedGridGraph* graph = nullptr);

    /**
     * @brief Answers reachability using hierarchical H-BRICK query steps.
     * @ingroup hbrick_baselines
     * @note Convenience single-threaded overload; mutates internal scratch buffers.
     */
    [[nodiscard]] ReachabilityAnswer query(uint32_t source, uint32_t target) const noexcept;

    /**
     * @brief Answers reachability using hierarchical H-BRICK query steps into caller-owned scratch.
     * @ingroup hbrick_baselines
     * @note Thread-safe for concurrent queries across threads sharing the same baseline.
     */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        HBrickQueryScratch& scratch,
        GraphSearchScratch& port_bfs_scratch
    ) const noexcept;

    /**
     * @brief Answers reachability using hierarchical H-BRICK query steps into caller-owned scratch with micro-BFS scratch.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        HBrickQueryScratch& scratch,
        GraphSearchScratch& port_bfs_scratch,
        GraphSearchScratch& micro_bfs_scratch
    ) const noexcept;

    /**
     * @brief Answers reachability and reports local hit, ancestor meet, and fallback.
     * @ingroup hbrick_baselines
     * @note Convenience single-threaded overload; mutates internal scratch buffers.
     */
    [[nodiscard]] HBrickQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target
    ) const noexcept;

    /**
     * @brief Answers reachability and reports details into caller-owned scratch.
     * @ingroup hbrick_baselines
     * @note Thread-safe for concurrent queries across threads sharing the same baseline.
     */
    [[nodiscard]] HBrickQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target,
        HBrickQueryScratch& scratch,
        GraphSearchScratch& port_bfs_scratch
    ) const noexcept;

    /**
     * @brief Answers reachability and reports details into caller-owned scratch with micro-BFS scratch.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] HBrickQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target,
        HBrickQueryScratch& scratch,
        GraphSearchScratch& port_bfs_scratch,
        GraphSearchScratch& micro_bfs_scratch
    ) const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

    /** @brief Returns the built index when preprocessing completed. @ingroup hbrick_baselines */
    [[nodiscard]] const HBrickIndex& index() const noexcept { return index_; }

    /**
     * @brief Returns hierarchical summary storage bytes after successful preprocessing.
     * @ingroup hbrick_baselines
     * @note Represents the benchmark-facing estimated retained storage bytes.
     */
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept;

    /**
     * @brief Returns exact audited retained heap bytes across all underlying index structures.
     * @ingroup hbrick_baselines
     * @note Directly delegates to HBrickIndex::measureStorageBytes().
     */
    [[nodiscard]] uint64_t measuredStorageBytes() const noexcept;

    /** @brief Returns query scratch sized during the last successful preprocess. @ingroup hbrick_baselines */
    [[nodiscard]] HBrickQueryScratch& scratch() noexcept { return scratch_; }
    [[nodiscard]] const HBrickQueryScratch& scratch() const noexcept { return scratch_; }

    /**
     * @brief Returns flat-BRICK port BFS scratch sized during the last successful preprocess.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] GraphSearchScratch& portBfsScratch() noexcept { return port_bfs_scratch_; }
    [[nodiscard]] const GraphSearchScratch& portBfsScratch() const noexcept {
        return port_bfs_scratch_;
    }

    /**
     * @brief Returns micro-BFS scratch sized during the last successful preprocess.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] GraphSearchScratch& microBfsScratch() noexcept { return micro_bfs_scratch_; }
    [[nodiscard]] const GraphSearchScratch& microBfsScratch() const noexcept {
        return micro_bfs_scratch_;
    }

    [[nodiscard]] static ReachabilityAnswer queryFlatBrickPortBfs(
        const BrickIndex& index,
        const DirectedGridGraph* graph,
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch,
        GraphSearchScratch& micro_bfs_scratch
    ) noexcept;

private:
    [[nodiscard]] static HBrickQueryOutcome queryHierarchical(
        const HBrickIndex& index,
        const DirectedGridGraph* graph,
        uint32_t source,
        uint32_t target,
        HBrickQueryScratch& scratch,
        GraphSearchScratch& micro_bfs_scratch
    ) noexcept;

    BaselineStatus status_ = BaselineStatus::NotRun;
    HBrickIndex index_{};
    const DirectedGridGraph* graph_ = nullptr;
    mutable HBrickQueryScratch scratch_{};
    mutable GraphSearchScratch port_bfs_scratch_{};
    mutable GraphSearchScratch micro_bfs_scratch_{};
};

}  // namespace hbrick
