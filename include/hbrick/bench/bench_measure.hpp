/**
 * @file bench_measure.hpp
 * @ingroup hbrick_bench
 * @brief Helper template for repeated micro-benchmark measurement.
 */

#pragma once

#include <cstdint>
#include <utility>

#include "hbrick/bench/bench_sample.hpp"
#include "hbrick/bench/bench_timer.hpp"

namespace hbrick {

/**
 * @brief Runs @p callable @p iterations times and records elapsed wall time.
 * @ingroup hbrick_bench
 *
 * @tparam Callable Zero-argument callable invoked repeatedly.
 * @param name Label stored in the returned @ref hbrick::BenchSample.
 * @param iterations Number of repetitions; may be zero.
 * @param callable Function or lambda under measurement.
 * @return Sample containing total elapsed time and iteration count.
 */
template <typename Callable>
BenchSample measureRepeated(std::string name, const uint64_t iterations, Callable&& callable) {
    BenchTimer timer;
    timer.start();
    for (uint64_t iteration = 0; iteration < iterations; ++iteration) {
        callable();
    }
    timer.stop();

    return BenchSample{
        std::move(name),
        iterations,
        timer.elapsedNanoseconds(),
    };
}

}  // namespace hbrick
