/**
 * @file hbrick_fused_lift_cache_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief H-BRICK baseline with fused tree lifts and bounded zero-allocation endpoint lift caching.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/baselines/hbrick_fused_lift_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/fused_tree_lifts.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hbrick_index.hpp"

namespace hbrick {

/**
 * @brief Pre-allocated cache slot for lifted endpoint vectors across hierarchy levels.
 * @ingroup hbrick_baselines
 *
 * @c lifted_through is the highest ancestor-chain index whose vector is valid.
 * A miss stores only the leaf (@c 0); higher lifts are filled lazily.
 */
struct EndpointLiftCacheSlot {
    uint32_t vertex_id = UINT32_MAX;
    uint32_t tile_id = UINT32_MAX;
    uint32_t lifted_through = 0U;
    bool has_active_leaf = false;
    std::vector<BitVector> chain;
};

/**
 * @brief Fixed-size, zero-allocation endpoint cache for source and target hierarchy lifts.
 * @ingroup hbrick_baselines
 *
 * Slots are mutated during query. The cache is single-threaded: one baseline
 * instance must not serve concurrent queries.
 */
class EndpointLiftCache {
public:
    EndpointLiftCache() = default;

    /** @brief Pre-allocates fixed slots matching hierarchy width and levels. */
    void init(uint32_t num_slots, const HBrickIndex& index);

    /** @brief Clears all cached entries. */
    void clear() noexcept;

    [[nodiscard]] uint32_t numSlots() const noexcept { return static_cast<uint32_t>(slots_.size()); }
    [[nodiscard]] EndpointLiftCacheSlot& slotFor(uint32_t vertex) noexcept {
        return slots_[(vertex * 2654435761U) % slots_.size()];
    }
    [[nodiscard]] const EndpointLiftCacheSlot& slotFor(uint32_t vertex) const noexcept {
        return slots_[(vertex * 2654435761U) % slots_.size()];
    }

    /** @brief Returns total memory consumed by cache slots and their bit vector chains. */
    [[nodiscard]] uint64_t memoryBytes() const noexcept;

private:
    std::vector<EndpointLiftCacheSlot> slots_;
};

/**
 * @brief H-BRICK reachability baseline with fused lifts and endpoint lift caching.
 * @ingroup hbrick_baselines
 *
 * Query mutates the endpoint caches and internal scratch; use one instance per
 * thread. Cache misses fill only the leaf port vector; ancestor lifts run
 * lazily to the same height the uncached fused query would compute.
 */
class HBrickFusedLiftCacheBaseline {
public:
    explicit HBrickFusedLiftCacheBaseline(uint32_t cache_slots = 512U);

    void preprocess(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        HBrickConfig config
    );

    void adoptPrebuiltIndex(HBrickIndex index, const DirectedGridGraph* graph = nullptr);

    [[nodiscard]] ReachabilityAnswer query(uint32_t source, uint32_t target) const noexcept;

    [[nodiscard]] HBrickQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target
    ) const noexcept;

    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept;
    [[nodiscard]] uint64_t measuredStorageBytes() const noexcept { return indexStorageBytes(); }
    [[nodiscard]] uint64_t baseIndexStorageBytes() const noexcept { return index_.measureStorageBytes(); }
    [[nodiscard]] uint64_t fusedLiftsMemoryBytes() const noexcept { return fused_lifts_.memoryBytes(); }
    [[nodiscard]] uint64_t endpointCacheMemoryBytes() const noexcept;
    [[nodiscard]] uint64_t scratchMemoryBytes() const noexcept;
    [[nodiscard]] const HBrickIndex& index() const noexcept { return index_; }
    [[nodiscard]] const FusedTreeLifts& fusedLifts() const noexcept { return fused_lifts_; }

    [[nodiscard]] HBrickQueryScratch& scratch() noexcept { return scratch_; }
    [[nodiscard]] const HBrickQueryScratch& scratch() const noexcept { return scratch_; }
    [[nodiscard]] GraphSearchScratch& portBfsScratch() noexcept { return port_bfs_scratch_; }
    [[nodiscard]] const GraphSearchScratch& portBfsScratch() const noexcept { return port_bfs_scratch_; }
    [[nodiscard]] GraphSearchScratch& microBfsScratch() noexcept { return micro_bfs_scratch_; }
    [[nodiscard]] const GraphSearchScratch& microBfsScratch() const noexcept { return micro_bfs_scratch_; }

    /** @brief Resets the endpoint cache (e.g. between isolated benchmark batches). */
    void clearCache() noexcept {
        source_cache_.clear();
        target_cache_.clear();
    }

private:
    uint32_t cache_capacity_slots_ = 512U;
    BaselineStatus status_ = BaselineStatus::NotRun;
    HBrickIndex index_{};
    FusedTreeLifts fused_lifts_{};
    const DirectedGridGraph* graph_ = nullptr;
    mutable HBrickQueryScratch scratch_{};
    mutable GraphSearchScratch port_bfs_scratch_{};
    mutable GraphSearchScratch micro_bfs_scratch_{};
    mutable EndpointLiftCache source_cache_{};
    mutable EndpointLiftCache target_cache_{};
};

}  // namespace hbrick
