/**
 * @file hbrick_micro_bfs_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief Memory-lean H-BRICK reachability baseline using intra-tile micro-BFS.
 */

#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/hbrick_config.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/**
 * @brief Memory-lean H-BRICK baseline with intra-tile micro-BFS.
 * @ingroup hbrick_baselines
 *
 * Replaces the dense intra-tile closure matrix (local_closure, taking O(b^4) bits)
 * with an allocation-free, cache-hot local micro-BFS inside the base tile.
 * Inter-tile queries continue to use hierarchical ancestor meet tests via R_VB/R_BV.
 */
class HBrickMicroBfsBaseline {
public:
    /**
     * @brief Builds the H-BRICK index with local_closure omitted.
     * @ingroup hbrick_baselines
     */
    void preprocess(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        HBrickConfig config
    );

    /** @brief Answers reachability using hierarchical query with micro-BFS fallback. */
    [[nodiscard]] ReachabilityAnswer query(uint32_t source, uint32_t target) const noexcept;

    /** @brief Answers reachability using caller-owned scratch buffers. */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        HBrickQueryScratch& scratch,
        GraphSearchScratch& port_bfs_scratch,
        GraphSearchScratch& micro_bfs_scratch
    ) const noexcept;

    /** @brief Answers reachability and reports detailed hit statistics. */
    [[nodiscard]] HBrickQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target
    ) const noexcept;

    /** @brief Answers reachability and reports details into caller-owned scratch. */
    [[nodiscard]] HBrickQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target,
        HBrickQueryScratch& scratch,
        GraphSearchScratch& port_bfs_scratch,
        GraphSearchScratch& micro_bfs_scratch
    ) const noexcept;

    [[nodiscard]] BaselineStatus status() const noexcept { return hbrick_.status(); }
    [[nodiscard]] const HBrickIndex& index() const noexcept { return hbrick_.index(); }
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept { return hbrick_.indexStorageBytes(); }
    [[nodiscard]] uint64_t measuredStorageBytes() const noexcept { return hbrick_.measuredStorageBytes(); }

    [[nodiscard]] HBrickQueryScratch& scratch() noexcept { return hbrick_.scratch(); }
    [[nodiscard]] const HBrickQueryScratch& scratch() const noexcept { return hbrick_.scratch(); }
    [[nodiscard]] GraphSearchScratch& portBfsScratch() noexcept { return hbrick_.portBfsScratch(); }
    [[nodiscard]] const GraphSearchScratch& portBfsScratch() const noexcept { return hbrick_.portBfsScratch(); }
    [[nodiscard]] GraphSearchScratch& microBfsScratch() noexcept { return hbrick_.microBfsScratch(); }
    [[nodiscard]] const GraphSearchScratch& microBfsScratch() const noexcept { return hbrick_.microBfsScratch(); }

    [[nodiscard]] const HBrickBaseline& underlyingBaseline() const noexcept { return hbrick_; }

private:
    HBrickBaseline hbrick_{};
};

}  // namespace hbrick
