/**
 * @file boolean_closure.hpp
 * @ingroup hbrick_bit
 * @brief Transitive closure algorithms on @ref hbrick::BitMatrix relations.
 */

#pragma once

#include "hbrick/bit/bit_matrix.hpp"

namespace hbrick {

/**
 * @brief Boolean matrix transitive-closure utilities.
 * @ingroup hbrick_bit
 *
 * Implements the Warshall algorithm for reachability preprocessing used by
 * @ref hbrick::FullClosureBaseline and related baseline code paths.
 */
class BooleanClosure {
public:
    /**
     * @brief Computes transitive closure in place using Warshall's algorithm.
     * @ingroup hbrick_bit
     *
     * On input, @p relation typically encodes a reflexive adjacency relation.
     * On output, @p relation[i][j] is @c true iff vertex @c i can reach @c j.
     *
     * @param relation Square or rectangular relation matrix updated in place.
     */
    static void transitiveClosureWarshallInPlace(BitMatrix& relation);

    /**
     * @brief Computes transitive closure into a new matrix copy.
     * @ingroup hbrick_bit
     *
     * @param relation Input relation matrix; left unchanged.
     * @return A new matrix containing the transitive closure of @p relation.
     */
    [[nodiscard]] static BitMatrix transitiveClosureWarshall(BitMatrix relation);
};

}  // namespace hbrick
