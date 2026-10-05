/**
 * @file hbrick_query_scratch.hpp
 * @ingroup hbrick_baselines
 * @brief Reusable bit-vector workspace for hierarchical H-BRICK queries.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/bit/bit_vector.hpp"
#include "hbrick/tile/region_node.hpp"

namespace hbrick {

class HBrickIndex;

/**
 * @brief Preallocated boolean vectors for @ref HBrickBaseline::query.
 * @ingroup hbrick_baselines
 *
 * Call @ref prepare once after preprocessing so query avoids heap allocation.
 */
class HBrickQueryScratch {
public:
    /**
     * @brief Sizes internal buffers for the built @p index.
     * @ingroup hbrick_baselines
     */
    void prepare(const HBrickIndex& index);

    /**
     * @brief Clears leaf attachment vectors (@c chain[0]) and gamma A/B/C.
     * @ingroup hbrick_baselines
     *
     * Ancestor id chains are not cleared here — @c buildAncestorChain rebuilds
     * them via @c clear + @c push_back on each query. Prefer this at query start;
     * clear higher bit-vector chain levels only through the LCA.
     */
    void clearLeafAndWorkspace() noexcept;

    /**
     * @brief Clears source/target bit-vector chain levels in @c [1, level_end).
     * @ingroup hbrick_baselines
     *
     * @p level_end is exclusive and must be the highest common ancestor level
     * (or less). Levels @c >= level_end are intentionally left untouched; callers
     * must not read them for the current query.
     */
    void clearChainLevelsThrough(uint32_t level_end) noexcept;

    /** @brief Source-side vectors indexed by hierarchy level. @ingroup hbrick_baselines */
    [[nodiscard]] std::vector<BitVector>& sourceChain() noexcept { return source_chain_; }
    [[nodiscard]] const std::vector<BitVector>& sourceChain() const noexcept {
        return source_chain_;
    }
    /** @brief Target-side vectors indexed by hierarchy level. @ingroup hbrick_baselines */
    [[nodiscard]] std::vector<BitVector>& targetChain() noexcept { return target_chain_; }
    [[nodiscard]] const std::vector<BitVector>& targetChain() const noexcept {
        return target_chain_;
    }
    /** @brief Temporary gamma workspace A. @ingroup hbrick_baselines */
    [[nodiscard]] BitVector& gammaA() noexcept { return gamma_a_; }
    /** @brief Temporary gamma workspace B. @ingroup hbrick_baselines */
    [[nodiscard]] BitVector& gammaB() noexcept { return gamma_b_; }
    /** @brief Temporary gamma workspace C. @ingroup hbrick_baselines */
    [[nodiscard]] BitVector& gammaC() noexcept { return gamma_c_; }

    /**
     * @brief Reusable ancestor chain from a base tile up to the root (source side).
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] std::vector<RegionNodeId>& sourceAncestorChain() noexcept {
        return source_ancestor_chain_;
    }
    [[nodiscard]] const std::vector<RegionNodeId>& sourceAncestorChain() const noexcept {
        return source_ancestor_chain_;
    }

    /**
     * @brief Reusable ancestor chain from a base tile up to the root (target side).
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] std::vector<RegionNodeId>& targetAncestorChain() noexcept {
        return target_ancestor_chain_;
    }
    [[nodiscard]] const std::vector<RegionNodeId>& targetAncestorChain() const noexcept {
        return target_ancestor_chain_;
    }

    /** @brief Heap bytes of bit-vector and ancestor-chain storage. @ingroup hbrick_baselines */
    [[nodiscard]] uint64_t memoryBytes() const noexcept;

private:
    std::vector<BitVector> source_chain_{};
    std::vector<BitVector> target_chain_{};
    std::vector<RegionNodeId> source_ancestor_chain_{};
    std::vector<RegionNodeId> target_ancestor_chain_{};
    BitVector gamma_a_{};
    BitVector gamma_b_{};
    BitVector gamma_c_{};
};

}  // namespace hbrick
