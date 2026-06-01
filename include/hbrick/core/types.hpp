/**
 * @file types.hpp
 * @ingroup hbrick_core
 * @brief Core enumerations shared across the hbrick library.
 */

#pragma once

#include <cstdint>

namespace hbrick {

/**
 * @brief Result of a single-pair reachability query.
 * @ingroup hbrick_core
 *
 * Returned by graph search routines, DAG reachability, and baseline
 * implementations when answering whether one vertex can reach another.
 */
enum class ReachabilityAnswer : uint8_t {
    /** @brief No directed path exists from source to target. @ingroup hbrick_core */
    Unreachable = 0,
    /** @brief At least one directed path exists from source to target. @ingroup hbrick_core */
    Reachable = 1
};

/**
 * @brief Outcome of a baseline preprocess or query lifecycle.
 * @ingroup hbrick_core
 *
 * Baseline classes record this status after @c preprocess so callers can
 * distinguish successful completion from policy skips, memory limits, or
 * internal failures without inspecting exception state.
 */
enum class BaselineStatus : uint8_t {
    /** @brief Preprocess has not been invoked yet. @ingroup hbrick_core */
    NotRun = 0,
    /** @brief Preprocess completed and queries may be issued. @ingroup hbrick_core */
    Completed,
    /** @brief Preprocess was skipped by an explicit policy decision. @ingroup hbrick_core */
    SkippedByPolicy,
    /** @brief Preprocess aborted because a memory budget would be exceeded. @ingroup hbrick_core */
    OutOfMemory,
    /** @brief Preprocess or query failed for a non-memory reason. @ingroup hbrick_core */
    Failed
};

}  // namespace hbrick
