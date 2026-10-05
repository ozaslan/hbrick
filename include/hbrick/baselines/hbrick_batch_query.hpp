/**
 * @file hbrick_batch_query.hpp
 * @ingroup hbrick_baselines
 * @brief Preallocated workspace and statistics for many-source / many-target
 *        H-BRICK reachability batches.
 *
 * A batch fills an @c m x n Boolean matrix @c R with @c R[i][j] = Reach(s_i, t_j)
 * while sharing per-endpoint hierarchical work: each source frontier is lifted,
 * embedded, and closed once per ancestor level, each target frontier once, and
 * each pair then costs one bit-vector intersection instead of a full lift/meet.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/bit/bit_vector.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/tile/region_node.hpp"

namespace hbrick {

class HBrickIndex;

/**
 * @brief Outcome counters recorded by one batch query call.
 * @ingroup hbrick_baselines
 */
struct HBrickBatchQueryStats {
    /** @brief Pairs in the batch (@c m * @c n). @ingroup hbrick_baselines */
    uint32_t pairs = 0U;
    /** @brief Pairs answered by the same-tile local closure or tile micro-BFS. @ingroup hbrick_baselines */
    uint32_t local_hits = 0U;
    /** @brief Pairs answered by an ancestor interface meet. @ingroup hbrick_baselines */
    uint32_t ancestor_hits = 0U;
    /** @brief Pairs that needed the flat BRICK port-BFS fallback. @ingroup hbrick_baselines */
    uint32_t fallbacks = 0U;
    /** @brief Pairs left negative by a sound hierarchy. @ingroup hbrick_baselines */
    uint32_t hierarchy_negatives = 0U;
    /** @brief Source-side skip-lift applications (once per source and level). @ingroup hbrick_baselines */
    uint32_t source_lifts = 0U;
    /** @brief Target-side skip-lift applications (once per target and level). @ingroup hbrick_baselines */
    uint32_t target_lifts = 0U;
    /** @brief Source-side closure projections (once per source and level). @ingroup hbrick_baselines */
    uint32_t source_projections = 0U;
    /** @brief Pair-level gamma intersections actually evaluated. @ingroup hbrick_baselines */
    uint32_t pair_tests = 0U;
    /** @brief Highest hierarchy level reached by any endpoint in this batch. @ingroup hbrick_baselines */
    uint32_t max_level_reached = 0U;
};

/**
 * @brief Pre-sized per-endpoint buffers for @ref HBrickSkipLiftBaseline::batchQuery.
 * @ingroup hbrick_baselines
 *
 * Call @ref prepare once after preprocessing. The batch query itself performs no
 * heap allocation: all frontier vectors, chains, and pair flags live here.
 * Callers that need re-entrant or concurrent batches can own one scratch per
 * thread and call the scratch-taking @ref HBrickSkipLiftBaseline::batchQuery
 * overload instead of sharing the baseline-internal workspace.
 */
class HBrickBatchScratch {
public:
    HBrickBatchScratch() = default;

    /**
     * @brief Sizes buffers for at most @p max_sources x @p max_targets endpoints.
     *
     * @param index Built H-BRICK index whose hierarchy defines the buffer sizes.
     * @param max_sources Maximum number of source endpoints per batch.
     * @param max_targets Maximum number of target endpoints per batch.
     */
    void prepare(
        const HBrickIndex& index,
        uint32_t max_sources,
        uint32_t max_targets
    );

    /** @brief Capacity in sources after @ref prepare. @ingroup hbrick_baselines */
    [[nodiscard]] uint32_t maxSources() const noexcept {
        return static_cast<uint32_t>(sources_.size());
    }

    /** @brief Capacity in targets after @ref prepare. @ingroup hbrick_baselines */
    [[nodiscard]] uint32_t maxTargets() const noexcept {
        return static_cast<uint32_t>(targets_.size());
    }

    /** @brief Hierarchy levels captured at @ref prepare time. @ingroup hbrick_baselines */
    [[nodiscard]] uint32_t numLevels() const noexcept { return num_levels_; }

    /** @brief Heap bytes retained by this scratch. @ingroup hbrick_baselines */
    [[nodiscard]] uint64_t memoryBytes() const noexcept;

private:
    friend class HBrickSkipLiftBaseline;

    struct Endpoint {
        bool valid = false;
        uint32_t tile = 0U;
        uint32_t local = 0U;
        uint32_t chain_length = 0U;
        /** @brief Highest ancestor-chain level whose lift and embedding are valid. */
        uint32_t embeddable_limit = 0U;
        /** @brief True when the leaf frontier is empty (cannot leave its tile). */
        bool confined = false;
        /** @brief Base-port frontier reachable from this endpoint inside its tile. */
        BitVector leaf{};
        /** @brief Embedded frontier in the current parent gamma space (input side). */
        BitVector gamma{};
        /** @brief Source-side projected frontier in the current parent gamma space. */
        BitVector projected{};
    };

    void resetEndpoints(
        uint32_t source_count,
        uint32_t target_count
    ) noexcept;

    uint32_t num_levels_ = 0U;
    uint32_t base_port_bits_ = 0U;
    uint32_t gamma_bits_ = 0U;
    uint32_t exterior_bits_ = 0U;
    uint64_t chain_stride_ = 0U;
    std::vector<Endpoint> sources_{};
    std::vector<Endpoint> targets_{};
    /** @brief Flat ancestor chains, @ref chain_stride_ entries per endpoint. */
    std::vector<RegionNodeId> source_chains_{};
    std::vector<RegionNodeId> target_chains_{};
    /** @brief Shared lift workspace sized to @ref exterior_bits_. */
    BitVector lift_workspace_{};
    /** @brief Per-pair smallest shared ancestor level (0 when none). */
    std::vector<uint8_t> pair_first_common_{};
    /** @brief Per-pair resolution flag. */
    std::vector<uint8_t> pair_resolved_{};
    /** @brief Whether source @c i has a non-empty projected frontier at the current level. */
    std::vector<uint8_t> source_has_level_{};
    /** @brief Whether target @c j has a non-empty embedded frontier at the current level. */
    std::vector<uint8_t> target_has_level_{};
    /** @brief Total number of unresolved pairs remaining across the batch. */
    uint64_t total_unresolved_ = 0U;
    /** @brief Caller-owned scratch for flat BRICK port BFS fallback. */
    GraphSearchScratch port_bfs_scratch_{};
    /** @brief Caller-owned scratch for same-tile micro-BFS fallback. */
    GraphSearchScratch micro_bfs_scratch_{};
};

}  // namespace hbrick
