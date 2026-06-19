/**
 * @file bench_timer.hpp
 * @ingroup hbrick_bench
 * @brief High-resolution elapsed-time measurement for micro-benchmarks.
 */

#pragma once

#include <cstdint>

namespace hbrick {

/**
 * @brief Simple stopwatch backed by a monotonic clock.
 * @ingroup hbrick_bench
 *
 * Used by @ref measureRepeated to time hot-path routines without pulling in
 * heavy benchmarking frameworks.
 */
class BenchTimer {
public:
    /** @brief Starts or restarts timing; clears any previous elapsed total. @ingroup hbrick_bench */
    void start() noexcept;

    /** @brief Stops timing and accumulates elapsed time since @ref start. @ingroup hbrick_bench */
    void stop() noexcept;

    /**
     * @brief Returns total elapsed time across completed start/stop intervals.
     * @ingroup hbrick_bench
     *
     * @return Elapsed duration in nanoseconds.
     */
    [[nodiscard]] uint64_t elapsedNanoseconds() const noexcept;

    /**
     * @brief Returns the current monotonic clock time in nanoseconds.
     * @ingroup hbrick_bench
     */
    [[nodiscard]] static uint64_t steadyNowNanoseconds() noexcept;

private:
    uint64_t start_nanoseconds_ = 0;
    uint64_t elapsed_nanoseconds_ = 0;
    bool running_ = false;
};

}  // namespace hbrick
