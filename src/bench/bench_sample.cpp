#include "hbrick/bench/bench_sample.hpp"

namespace hbrick {

double BenchSample::nanosecondsPerIteration() const noexcept {
    if (iterations == 0) {
        return 0.0;
    }
    return static_cast<double>(elapsed_nanoseconds) / static_cast<double>(iterations);
}

}  // namespace hbrick
