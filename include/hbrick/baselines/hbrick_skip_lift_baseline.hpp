/**
 * @file hbrick_skip_lift_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief H-BRICK baseline with precomputed skip-level fused lift products.
 */

#pragma once

#include <cstdint>
#include <span>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/baselines/hbrick_batch_query.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hbrick_index.hpp"
#include "hbrick/tile/skip_level_lifts.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/**
 * @brief H-BRICK reachability using skip-level products of fused tree-edge lifts.
 * @ingroup hbrick_baselines
 *
 * Preprocess composes each ancestor-path of one-hop fused operators into a
 * single rectangular matrix from base-tile ports to that ancestor's exterior.
 * Query lifts a leaf vector to any common-ancestor child in one matrix-vector
 * product, then runs the same bilinear interface meet as stock H-BRICK.
 */
class HBrickSkipLiftBaseline {
public:
    void preprocess(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        HBrickConfig config
    );

    void adoptPrebuiltIndex(HBrickIndex index, const DirectedGridGraph* graph = nullptr);

    [[nodiscard]] ReachabilityAnswer query(uint32_t source, uint32_t target) const noexcept;

    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        HBrickQueryScratch& scratch,
        GraphSearchScratch& port_bfs_scratch,
        GraphSearchScratch& micro_bfs_scratch
    ) const noexcept;

    [[nodiscard]] HBrickQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target
    ) const noexcept;

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
    [[nodiscard]] uint64_t skipLiftsMemoryBytes() const noexcept { return skip_lifts_.memoryBytes(); }
    [[nodiscard]] uint64_t scratchMemoryBytes() const noexcept;
    [[nodiscard]] const HBrickIndex& index() const noexcept { return index_; }
    [[nodiscard]] const SkipLevelLifts& skipLifts() const noexcept { return skip_lifts_; }

    /**
     * @brief Sizes the batch workspace for at most @p max_sources x @p max_targets.
     * @ingroup hbrick_baselines
     *
     * Allocation happens here, never inside @ref batchQuery. Returns @c false when
     * the baseline is not completed.
     */
    [[nodiscard]] bool prepareBatch(uint32_t max_sources, uint32_t max_targets);

    /**
     * @brief Fills the @c m x n reachability matrix for a source/target batch.
     * @ingroup hbrick_baselines
     *
     * @c out_reachable[i * targets.size() + j] is set to @c 1 when @c targets[j]
     * is reachable from @c sources[i], and @c 0 otherwise. Equivalent to running
     * @ref queryDetailed for every ordered pair, but each endpoint frontier is
     * lifted, embedded, and (source side) closed once per ancestor level.
     *
     * @param sources Source vertex ids (passable vertices).
     * @param targets Target vertex ids (passable vertices).
     * @return @c true when the batch was executed, or @c false if rejected
     *         due to invalid status, undersized output, or incompatible scratch.
     */
    bool batchQuery(
        std::span<const uint32_t> sources,
        std::span<const uint32_t> targets,
        std::span<uint8_t> out_reachable,
        HBrickBatchQueryStats* stats = nullptr
    ) const noexcept;

    /**
     * @brief Same batch with a caller-owned workspace for re-entrant use.
     * @ingroup hbrick_baselines
     *
     * Pass a scratch that was prepared with @ref HBrickBatchScratch::prepare for
     * at least @c sources.size() x @c targets.size() endpoints. One scratch per
     * thread keeps concurrent batches on the same baseline race-free, because
     * the call then touches no baseline-internal mutable state.
     *
     * @return @c true when the batch was executed, or @c false if rejected.
     */
    bool batchQuery(
        HBrickBatchScratch& scratch,
        std::span<const uint32_t> sources,
        std::span<const uint32_t> targets,
        std::span<uint8_t> out_reachable,
        HBrickBatchQueryStats* stats = nullptr
    ) const noexcept;

    /** @brief Heap bytes retained by the batch workspace. @ingroup hbrick_baselines */
    [[nodiscard]] uint64_t batchScratchMemoryBytes() const noexcept {
        return batch_scratch_.memoryBytes();
    }

    [[nodiscard]] HBrickQueryScratch& scratch() noexcept { return scratch_; }
    [[nodiscard]] const HBrickQueryScratch& scratch() const noexcept { return scratch_; }
    [[nodiscard]] GraphSearchScratch& portBfsScratch() noexcept { return port_bfs_scratch_; }
    [[nodiscard]] const GraphSearchScratch& portBfsScratch() const noexcept { return port_bfs_scratch_; }
    [[nodiscard]] GraphSearchScratch& microBfsScratch() noexcept { return micro_bfs_scratch_; }
    [[nodiscard]] const GraphSearchScratch& microBfsScratch() const noexcept { return micro_bfs_scratch_; }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    HBrickIndex index_{};
    SkipLevelLifts skip_lifts_{};
    const DirectedGridGraph* graph_ = nullptr;
    mutable HBrickQueryScratch scratch_{};
    mutable GraphSearchScratch port_bfs_scratch_{};
    mutable GraphSearchScratch micro_bfs_scratch_{};
    mutable HBrickBatchScratch batch_scratch_{};
};

}  // namespace hbrick
