/**
 * @file boolean_closure.hpp
 * @ingroup hbrick_bit
 * @brief Transitive closure algorithms on @ref hbrick::BitMatrix relations.
 */

#pragma once

#include <cstdint>

#include "hbrick/bit/bit_matrix.hpp"

namespace hbrick {

/**
 * @brief Boolean matrix product @c C = A * B in the path semiring.
 * @ingroup hbrick_bit
 */
[[nodiscard]] BitMatrix booleanMultiply(const BitMatrix& lhs, const BitMatrix& rhs);

/**
 * @brief Returns whether @p lhs and @p rhs have identical dimensions and bits.
 * @ingroup hbrick_bit
 */
[[nodiscard]] bool bitMatricesEqual(const BitMatrix& lhs, const BitMatrix& rhs) noexcept;

/**
 * @brief Boolean matrix transitive-closure utilities.
 * @ingroup hbrick_bit
 *
 * Provides Kleene-star closure via repeated squaring of @c I | A and Warshall
 * for regression oracles.
 */
class BooleanClosure {
public:
    /**
     * @brief Squaring rounds for Kleene closure from largest component order.
     * @ingroup hbrick_bit
     *
     * @return @c 0 when @p largest_component_size is at most @c 1; otherwise
     *         @c ceil(log2(largest_component_size)).
     */
    [[nodiscard]] static uint32_t kleeneSquaringCountForLargestComponent(
        uint32_t largest_component_size
    ) noexcept;

    /**
     * @brief Computes transitive closure in place by repeated squaring.
     * @ingroup hbrick_bit
     *
     * On input, @p relation encodes @c B = I | A. Performs up to @p squaring_count
     * boolean squarings @c R <- R * R, stopping early on fixpoint.
     *
     * @param relation Square reflexive adjacency updated to closure on output.
     * @param squaring_count Maximum number of squaring steps.
     * @param scratch Optional reusable M×M buffer; resized when dimensions change.
     */
    static void transitiveClosureKleeneSquaringInPlace(
        BitMatrix& relation,
        uint32_t squaring_count,
        BitMatrix* scratch = nullptr
    );

    /**
     * @brief Computes transitive closure in place using Warshall's algorithm.
     * @ingroup hbrick_bit
     *
     * Oracle / regression only; production L0 paths use Kleene squaring.
     */
    static void transitiveClosureWarshallInPlace(BitMatrix& relation);

    /**
     * @brief Computes transitive closure into a new matrix copy.
     * @ingroup hbrick_bit
     */
    [[nodiscard]] static BitMatrix transitiveClosureWarshall(BitMatrix relation);
};

}  // namespace hbrick
