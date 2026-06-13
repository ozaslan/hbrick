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
inline constexpr uint32_t kMovingAiMapReachabilityTimeoutSeconds = 180U;

}  // namespace hbrick::test_support
