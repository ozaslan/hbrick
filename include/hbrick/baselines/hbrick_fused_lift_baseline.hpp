/**
 * @file hbrick_fused_lift_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief H-BRICK baseline with precomputed rectangular tree-edge lift operators.
 */

#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/fused_tree_lifts.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hbrick_index.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/**
 * @brief H-BRICK reachability baseline using precomputed rectangular tree-edge lift operators.
 * @ingroup hbrick_baselines
 *
 * Replaces iterative scatter/gather interface closures with direct contiguous row-OR operations.
 */
class HBrickFusedLiftBaseline {
public:
    /**
     * @brief Preprocesses the grid graph by building HBrickIndex and precomputing fused lifts.
     */
    void preprocess(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        HBrickConfig config
    );

    /**
     * @brief Adopts a pre-built index and builds fused lifts for query use.
     */
    void adoptPrebuiltIndex(HBrickIndex index, const DirectedGridGraph* graph = nullptr);

    /** @brief Single-threaded query using internal scratch. */
    [[nodiscard]] ReachabilityAnswer query(uint32_t source, uint32_t target) const noexcept;

    /** @brief Thread-safe query using caller-owned scratch. */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        HBrickQueryScratch& scratch,
        GraphSearchScratch& port_bfs_scratch,
        GraphSearchScratch& micro_bfs_scratch
    ) const noexcept;

    /** @brief Query with hit statistics. */
    [[nodiscard]] HBrickQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target
    ) const noexcept;

    /** @brief Query with hit statistics into caller-owned scratch. */
    [[nodiscard]] HBrickQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target,
        HBrickQueryScratch& scratch,
        GraphSearchScratch& port_bfs_scratch,
        GraphSearchScratch& micro_bfs_scratch
    ) const noexcept;

    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept;
    [[nodiscard]] uint64_t measuredStorageBytes() const noexcept { return indexStorageBytes(); }
    [[nodiscard]] uint64_t baseIndexStorageBytes() const noexcept { return index_.measureStorageBytes(); }
    [[nodiscard]] uint64_t fusedLiftsMemoryBytes() const noexcept { return fused_lifts_.memoryBytes(); }
    [[nodiscard]] uint64_t scratchMemoryBytes() const noexcept;
    [[nodiscard]] const HBrickIndex& index() const noexcept { return index_; }
    [[nodiscard]] const FusedTreeLifts& fusedLifts() const noexcept { return fused_lifts_; }

    [[nodiscard]] HBrickQueryScratch& scratch() noexcept { return scratch_; }
    [[nodiscard]] const HBrickQueryScratch& scratch() const noexcept { return scratch_; }
    [[nodiscard]] GraphSearchScratch& portBfsScratch() noexcept { return port_bfs_scratch_; }
    [[nodiscard]] const GraphSearchScratch& portBfsScratch() const noexcept { return port_bfs_scratch_; }
    [[nodiscard]] GraphSearchScratch& microBfsScratch() noexcept { return micro_bfs_scratch_; }
    [[nodiscard]] const GraphSearchScratch& microBfsScratch() const noexcept { return micro_bfs_scratch_; }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    HBrickIndex index_{};
    FusedTreeLifts fused_lifts_{};
    const DirectedGridGraph* graph_ = nullptr;
    mutable HBrickQueryScratch scratch_{};
    mutable GraphSearchScratch port_bfs_scratch_{};
    mutable GraphSearchScratch micro_bfs_scratch_{};
};

}  // namespace hbrick
