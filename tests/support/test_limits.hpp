/**
 * @file test_limits.hpp
 * @ingroup hbrick_test_support
 * @brief Shared size and slicing limits so each Google Test case stays fast.
 */

#pragma once

#include <cstdint>

namespace hbrick::test_support {

/** @brief All-pairs baseline checks run in one test when vertex count is at most this. @ingroup hbrick_test_support */
inline constexpr uint32_t kFullAllPairsVertexLimit = 600U;

/**
 * @brief Pair volume matched by @ref kFullAllPairsVertexLimit all-pairs tests (V² at the limit).
 * @ingroup hbrick_test_support
 */
inline constexpr uint64_t kFullAllPairsPairCount =
    static_cast<uint64_t>(kFullAllPairsVertexLimit) * kFullAllPairsVertexLimit;

/** @brief Maximum ordered (source, target) pairs checked in one slice. @ingroup hbrick_test_support */
inline constexpr uint32_t kMaxPairsPerSlice = 250000U;

/**
 * @brief Maximum pair slices executed inside a single Google Test case.
 * @ingroup hbrick_test_support
 *
 * All slices for one scenario run sequentially in one test (not as separate CTest entries).
 * If @c ceil(V² / kMaxPairsPerSlice) exceeds this, the test fails with guidance to raise
 * @ref kMaxPairsPerSlice.
 */
inline constexpr uint32_t kMaxSlicesPerTestCase = 64U;

/** @brief Target upper bound for any single Google Test case (enforced via CTest TIMEOUT). @ingroup hbrick_test_support */
inline constexpr uint32_t kPerTestTimeoutSeconds = 20U;

/** @brief CTest timeout for per-map MovingAI reachability integration tests. @ingroup hbrick_test_support */
inline constexpr uint32_t kMovingAiMapReachabilityTimeoutSeconds = 60U;

/**
 * @brief Maximum vertices for a map in the default (smoke) MovingAI reachability catalog.
 * @ingroup hbrick_test_support
 *
 * Sets whose smallest extracted map exceeds this limit are omitted from
 * @ref movingAiReachabilityTestCatalog() unless @c HBRICK_FULL_MOVINGAI_TESTS is enabled.
 */
inline constexpr uint32_t kMovingAiSmokeMapVertexLimit = 10000U;

/**
 * @brief Ordered (source, target) pairs checked per catalog/recipe integration test.
 * @ingroup hbrick_test_support
 *
 * Pair indices are chosen at evenly spaced positions in @c [0, V²).
 */
inline constexpr uint32_t kIntegrationReachabilitySamplePairCount = 1000U;

/**
 * @brief Maximum vertices for SCC and BRICK checks inside @ref expectReachabilityOracleSampledPairs.
 * @ingroup hbrick_test_support
 *
 * Larger graphs still receive @ref kIntegrationReachabilitySamplePairCount search-baseline
 * pair checks only.
 */
inline constexpr uint32_t kSampledOracleFullCheckVertexLimit = kMovingAiSmokeMapVertexLimit;

/** @deprecated Use @ref kIntegrationReachabilitySamplePairCount. @ingroup hbrick_test_support */
inline constexpr uint32_t kMovingAiReachabilitySamplePairCount =
    kIntegrationReachabilitySamplePairCount;

}  // namespace hbrick::test_support
