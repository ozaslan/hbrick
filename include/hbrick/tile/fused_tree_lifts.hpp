/**
 * @file fused_tree_lifts.hpp
 * @ingroup hbrick_tile
 * @brief Precomputed rectangular forward and reverse tree-edge lift operators.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/bit/bit_vector.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/super_tile_summary.hpp"

namespace hbrick {

class HBrickIndex;

/**
 * @brief Precomputed rectangular lift operators for one parent-child hierarchy edge.
 * @ingroup hbrick_tile
 *
 * Fuses child embedding, interface closure multiplication, and exterior boundary projection:
 * - @ref forward maps child boundary ports directly to parent exterior ports.
 * - @ref reverse_transpose maps child boundary ports directly to parent exterior ports
 *   in row-major form, enabling identical contiguous row-OR operations during reverse lifting.
 */
struct FusedChildLift {
    /** @brief Forward lift operator: row p = parent exterior ports reachable from child port p. */
    BitMatrix forward;
    /** @brief Reverse lift transpose: row p = parent exterior ports that can reach child port p. */
    BitMatrix reverse_transpose;
};

/**
 * @brief Lift operators for all active children of one super-tile parent region.
 * @ingroup hbrick_tile
 */
struct FusedParentLift {
    /** @brief Lifts indexed by child embedding index. */
    std::vector<FusedChildLift> children;
};

/**
 * @brief Complete collection of fused tree-edge lift operators across all hierarchy levels.
 * @ingroup hbrick_tile
 */
class FusedTreeLifts {
public:
    FusedTreeLifts() = default;

    /**
     * @brief Builds fused lift operators for all levels in @p index.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] static FusedTreeLifts build(const HBrickIndex& index);

    /** @brief Returns whether fused lifts were built successfully. */
    [[nodiscard]] bool isValid() const noexcept { return is_valid_; }

    /** @brief Returns total memory consumed by all lift matrices. */
    [[nodiscard]] uint64_t memoryBytes() const noexcept;

    /**
     * @brief Lifts a source child port vector to the parent exterior boundary.
     * @param level Hierarchy level of the parent (level >= 1).
     * @param parent_region Region index of the parent.
     * @param child_embedding_index Child embedding slot.
     * @param child_vector Active ports on the child boundary.
     * @param out_parent Output vector on the parent exterior boundary.
     */
    void liftSource(
        uint32_t level,
        uint32_t parent_region,
        uint32_t child_embedding_index,
        const BitVector& child_vector,
        BitVector& out_parent
    ) const noexcept;

    /**
     * @brief Lifts a target child port vector to the parent exterior boundary.
     * @param level Hierarchy level of the parent (level >= 1).
     * @param parent_region Region index of the parent.
     * @param child_embedding_index Child embedding slot.
     * @param child_vector Active ports on the child boundary.
     * @param out_parent Output vector on the parent exterior boundary.
     */
    void liftTarget(
        uint32_t level,
        uint32_t parent_region,
        uint32_t child_embedding_index,
        const BitVector& child_vector,
        BitVector& out_parent
    ) const noexcept;

    /**
     * @brief One-hop fused operator for a child embedding, or @c nullptr if missing.
     */
    [[nodiscard]] const FusedChildLift* childLift(
        uint32_t level,
        uint32_t parent_region,
        uint32_t child_embedding_index
    ) const noexcept;

private:
    bool is_valid_ = false;
    // levels_[level][region_index]
    std::vector<std::vector<FusedParentLift>> levels_;
};

}  // namespace hbrick
