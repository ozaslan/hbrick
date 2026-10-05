#include <gtest/gtest.h>

#include "hbrick/bench/bench_measure.hpp"
#include "hbrick/bench/bench_timer.hpp"

TEST(BenchTimer, MeasuresElapsedTime) {
    hbrick::BenchTimer timer;
    timer.start();

    volatile uint64_t sum = 0;
    for (uint64_t value = 0; value < 100000U; ++value) {
        sum += value;
    }

    timer.stop();
    EXPECT_GT(timer.elapsedNanoseconds(), 0U);
    EXPECT_EQ(sum, 4999950000ULL);
}

TEST(BenchMeasure, ComputesAveragePerIteration) {
    const hbrick::BenchSample sample = hbrick::measureRepeated("increment", 1000U, []() {
        volatile uint32_t counter = 0;
        counter += 1U;
        static_cast<void>(counter);
    });

    EXPECT_EQ(sample.name, "increment");
    EXPECT_EQ(sample.iterations, 1000U);
    EXPECT_GT(sample.elapsed_nanoseconds, 0U);
    EXPECT_GT(sample.nanosecondsPerIteration(), 0.0);
}
