/**
 * @file random_asymmetric_params.hpp
 * @ingroup hbrick_graph
 * @brief Parameters for seeded random asymmetric grid edge conversion.
 */

#pragma once

#include <cstdint>

namespace hbrick {

/**
 * @brief Randomness controls for @ref hbrick::GridEdgeConversionMode::RandomAsymmetric.
 * @ingroup hbrick_graph
 *
 * Probabilities apply independently per candidate adjacency when converting a
 * @ref hbrick::MazeLayout into a directed graph.
 */
struct RandomAsymmetricParams {
    /** @brief Seed for the deterministic pseudo-random edge-orientation process. @ingroup hbrick_graph */
    uint64_t seed = 0;
    /** @brief Probability of replacing a one-way arc with bidirectional edges. @ingroup hbrick_graph */
    long double p_bidirectional = 0.0L;
    /** @brief Probability of orienting an adjacency as a single directed arc. @ingroup hbrick_graph */
    long double p_one_way = 0.0L;
};

/**
 * @brief Policy for turning grid adjacencies into directed edges.
 * @ingroup hbrick_graph
 */
enum class GridEdgeConversionMode : uint8_t {
    /**
     * @brief Seeded random mix of one-way and bidirectional orientations.
     * @ingroup hbrick_graph
     *
     * Uses @ref hbrick::RandomAsymmetricParams for probabilities and seeding.
     */
    RandomAsymmetric = 0,
    /** @brief Every passable adjacency becomes two opposing directed edges. @ingroup hbrick_graph */
    BidirectionalAll,
    /**
     * @brief Orient each adjacency east or south only, yielding an acyclic graph.
     * @ingroup hbrick_graph
     *
     * Matches the east/south scan order of
     * @ref hbrick::MazeLayout::forEachPassableAdjacentPairEastSouth.
     */
    AcyclicEastSouth
};

}  // namespace hbrick
