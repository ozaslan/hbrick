/**
 * @file skip_level_lifts.hpp
 * @ingroup hbrick_tile
 * @brief Precomputed skip-level products of fused tree-edge lift operators.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/bit/bit_vector.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/fused_tree_lifts.hpp"

namespace hbrick {

class HBrickIndex;

/**
 * @brief Skip-level lift operators from one base tile to each of its ancestors.
 * @ingroup hbrick_tile
 *
 * @c by_level[k] for @c k >= 1 maps that tile's base ports directly to the
 * exterior ports of ancestor chain index @c k. It is the Boolean product of
 * the one-hop fused operators along the path. Index @c 0 is unused.
 */
struct SkipTileLifts {
    std::vector<FusedChildLift> by_level;
};

/**
 * @brief Per-base-tile skip-level products of fused one-hop lifts.
 * @ingroup hbrick_tile
 *
 * Query applies one matrix-vector product from the leaf port vector to any
 * ancestor exterior instead of iterating one hop at a time.
 */
class SkipLevelLifts {
public:
    SkipLevelLifts() = default;

    /**
     * @brief Composes one-hop @p fused_lifts along every base-tile ancestor chain.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] static SkipLevelLifts build(
        const HBrickIndex& index,
        const FusedTreeLifts& fused_lifts
    );

    [[nodiscard]] bool isValid() const noexcept { return is_valid_; }
    [[nodiscard]] uint64_t memoryBytes() const noexcept;

    /**
     * @brief Lifts a source leaf port vector to ancestor exterior at @p dest_level.
     *
     * @p dest_level is the ancestor-chain index (@c >= 1). @p out_parent must
     * already be sized; this call allocates nothing.
     */
    void liftSource(
        uint32_t base_tile,
        uint32_t dest_level,
        const BitVector& leaf_vector,
        BitVector& out_parent
    ) const noexcept;

    /**
     * @brief Lifts a target leaf port vector to ancestor exterior at @p dest_level.
     */
    void liftTarget(
        uint32_t base_tile,
        uint32_t dest_level,
        const BitVector& leaf_vector,
        BitVector& out_parent
    ) const noexcept;

    [[nodiscard]] const SkipTileLifts* tileLifts(uint32_t base_tile) const noexcept;

private:
    bool is_valid_ = false;
    std::vector<SkipTileLifts> tiles_;
};

}  // namespace hbrick
