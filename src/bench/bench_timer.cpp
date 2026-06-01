#include "hbrick/bench/bench_timer.hpp"

#include <chrono>

namespace hbrick {

namespace {

[[nodiscard]] uint64_t nowNanoseconds() noexcept {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count()
    );
}

}  // namespace

void BenchTimer::start() noexcept {
    start_nanoseconds_ = nowNanoseconds();
    running_ = true;
}

void BenchTimer::stop() noexcept {
    if (!running_) {
        return;
    }

    elapsed_nanoseconds_ = nowNanoseconds() - start_nanoseconds_;
    running_ = false;
}

uint64_t BenchTimer::elapsedNanoseconds() const noexcept {
    return elapsed_nanoseconds_;
}

}  // namespace hbrick
