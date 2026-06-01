/**
 * @file bench_sample.hpp
 * @ingroup hbrick_bench
 * @brief Named benchmark result with iteration count and elapsed time.
 */

#pragma once

#include <cstdint>
#include <string>

namespace hbrick {

/**
 * @brief Aggregated timing result for a repeated benchmark invocation.
 * @ingroup hbrick_bench
 *
 * Produced by @ref measureRepeated and consumed by benchmark reporting code.
 */
struct BenchSample {
    /** @brief Human-readable label for the measured routine or scenario. @ingroup hbrick_bench */
    std::string name;
    /** @brief Number of times the callable was executed. @ingroup hbrick_bench */
    uint64_t iterations = 0;
    /** @brief Total elapsed time across all iterations, in nanoseconds. @ingroup hbrick_bench */
    uint64_t elapsed_nanoseconds = 0;

    /**
     * @brief Returns mean time per iteration.
     * @ingroup hbrick_bench
     *
     * @return Average nanoseconds per iteration, or @c 0.0 when @ref iterations is zero.
     */
    [[nodiscard]] double nanosecondsPerIteration() const noexcept;
};

}  // namespace hbrick
