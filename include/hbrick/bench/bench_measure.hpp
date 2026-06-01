#pragma once

#include <cstdint>
#include <utility>

#include "hbrick/bench/bench_sample.hpp"
#include "hbrick/bench/bench_timer.hpp"

namespace hbrick {

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
