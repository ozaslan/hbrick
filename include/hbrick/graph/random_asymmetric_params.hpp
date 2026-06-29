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
    /**
     * @brief Flow direction in degrees for @ref hbrick::GridEdgeConversionMode::GradientFlow.
     * @ingroup hbrick_graph
     *
     * Grid convention: 0 points east (+x), 90 points south (+y, increasing row).
     */
    double gradient_angle_degrees = 45.0;
    /**
     * @brief Probability of flipping a one-way arc against the gradient in
     *        @ref hbrick::GridEdgeConversionMode::GradientFlow.
     * @ingroup hbrick_graph
     *
     * Small backflow noise creates local cycles and therefore non-singleton
     * SCCs inside an otherwise DAG-like flow field.
     */
    long double p_against_gradient = 0.02L;
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
    AcyclicEastSouth,
    /**
     * @brief Orient adjacencies along a global flow direction with noise.
     * @ingroup hbrick_graph
     *
     * Every passable adjacency yields at least one arc: bidirectional with
     * @ref hbrick::RandomAsymmetricParams::p_bidirectional, otherwise a single arc pointing
     * along the projection of @ref hbrick::RandomAsymmetricParams::gradient_angle_degrees,
     * flipped against the flow with
     * @ref hbrick::RandomAsymmetricParams::p_against_gradient. Produces coherent
     * "downhill" reachability with a tunable amount of backflow cycles.
     */
    GradientFlow
};

/**
 * @brief Clamps invalid probability inputs to finite values in @c [0, 1].
 * @ingroup hbrick_graph
 *
 * Non-finite values become @c 0. For random-asymmetric modes,
 * @ref RandomAsymmetricParams::p_bidirectional and
 * @ref RandomAsymmetricParams::p_one_way are scaled down when their sum exceeds @c 1.
 */
[[nodiscard]] RandomAsymmetricParams sanitizeRandomAsymmetricParams(
    RandomAsymmetricParams params
) noexcept;

}  // namespace hbrick
