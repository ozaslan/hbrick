/**
 * @file random_asymmetric_params.hpp
 * @ingroup hbrick_graph
 * @brief Parameters for seeded random asymmetric grid edge conversion.
 */

#pragma once

#include <cstdint>
#include <string>

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
    /**
     * @brief Probability of leaving a passable adjacency with no directed arc.
     * @ingroup hbrick_graph
     *
     * Negative (the default) keeps the legacy three-way mix: bidirectional,
     * one-way, then implicit drop of the remainder. Zero means "do not drop":
     * leftover probability after @ref p_one_way and @ref p_bidirectional becomes
     * bidirectional. Use this so @c p_one_way=0.05 is 5% one-way / 95% two-way,
     * not "keep 5% of adjacencies".
     */
    long double p_edge_drop = -1.0L;
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

/**
 * @brief Resolves the three-way mix (bidirectional / one-way / drop).
 * @ingroup hbrick_graph
 *
 * When @ref RandomAsymmetricParams::p_edge_drop is negative, leftover
 * probability after one-way and bidirectional is drop (legacy). When it is
 * non-negative, leftover probability is bidirectional.
 */
[[nodiscard]] RandomAsymmetricParams resolveRandomAsymmetricMix(
    RandomAsymmetricParams params
) noexcept;

/**
 * @brief Expected directed arcs per undirected passable adjacency after resolve.
 * @ingroup hbrick_graph
 */
[[nodiscard]] long double expectedDirectedArcsPerPair(
    const RandomAsymmetricParams& params
) noexcept;

/**
 * @brief Rounded expected directed-arc count for @p undirected_pairs adjacencies.
 * @ingroup hbrick_graph
 */
[[nodiscard]] uint64_t expectedDirectedArcCount(
    uint64_t undirected_pairs,
    const RandomAsymmetricParams& params
) noexcept;

/**
 * @brief Returns whether @p observed_arcs is within a sampling band of the mix.
 * @ingroup hbrick_graph
 */
[[nodiscard]] bool randomAsymmetricArcCountMatchesExpectation(
    uint64_t observed_arcs,
    uint64_t undirected_pairs,
    const RandomAsymmetricParams& params,
    std::string& error_message
);

/**
 * @brief Completes orientation so every adjacency has at least one arc.
 * @ingroup hbrick_graph
 *
 * Sets @ref RandomAsymmetricParams::p_edge_drop to zero. If only
 * @ref RandomAsymmetricParams::p_one_way is set, fills bidirectional as the
 * complement. If @ref RandomAsymmetricParams::p_bidirectional is set, fills
 * one-way as the complement (manuscript convention).
 */
[[nodiscard]] RandomAsymmetricParams completeRandomAsymmetricOrientation(
    RandomAsymmetricParams params
) noexcept;

/**
 * @brief Returns whether @c p_one_way + p_bidirectional equals 1 within @p epsilon.
 * @ingroup hbrick_graph
 */
[[nodiscard]] bool randomAsymmetricOrientationIsComplete(
    const RandomAsymmetricParams& params,
    long double epsilon = 1.0e-6L
) noexcept;

/**
 * @brief Fails when @c |p_one + p_bi - 1| exceeds @c 1e-6, then completes the complement.
 * @ingroup hbrick_graph
 *
 * Paper-campaign import/rebuild must not silently rescale an incomplete pair.
 */
[[nodiscard]] bool enforceCompleteRandomAsymmetricOrientation(
    RandomAsymmetricParams& params,
    std::string& error_message
);

}  // namespace hbrick
