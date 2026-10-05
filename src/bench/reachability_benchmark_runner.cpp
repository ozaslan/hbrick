#include "hbrick/bench/reachability_benchmark_runner.hpp"

#include <chrono>
#include <utility>

namespace hbrick {

struct ReachabilityBenchmarkRunner::Impl {
    ReachabilityBenchmarkJob job;
    std::chrono::steady_clock::time_point started_at{};
    std::chrono::steady_clock::time_point ended_at{};
    bool timer_frozen = false;

    void freezeTimerIfNeeded() noexcept {
        if (!timer_frozen && started_at.time_since_epoch().count() != 0) {
            ended_at = std::chrono::steady_clock::now();
            timer_frozen = true;
        }
    }
};

ReachabilityBenchmarkRunner::ReachabilityBenchmarkRunner()
    : impl_(std::make_unique<Impl>()) {}

ReachabilityBenchmarkRunner::~ReachabilityBenchmarkRunner() {
    cancel();
}

ReachabilityBenchmarkRunner::ReachabilityBenchmarkRunner(
    ReachabilityBenchmarkRunner&&
) noexcept = default;

ReachabilityBenchmarkRunner& ReachabilityBenchmarkRunner::operator=(
    ReachabilityBenchmarkRunner&&
) noexcept = default;

void ReachabilityBenchmarkRunner::begin(
    const CsrGraph& graph,
    const std::span<const uint32_t> query_universe,
    ReachabilityBenchmarkConfig config
) {
    cancel();

    impl_->timer_frozen = false;
    impl_->started_at = {};
    impl_->ended_at = {};

    impl_->job.begin(graph, query_universe, std::move(config));
    if (!impl_->job.active()) {
        return;
    }

    impl_->started_at = std::chrono::steady_clock::now();
}

void ReachabilityBenchmarkRunner::begin(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const std::span<const uint32_t> query_universe,
    ReachabilityBenchmarkConfig config
) {
    cancel();

    impl_->timer_frozen = false;
    impl_->started_at = {};
    impl_->ended_at = {};

    impl_->job.begin(graph, layout, query_universe, std::move(config));
    if (!impl_->job.active()) {
        return;
    }

    impl_->started_at = std::chrono::steady_clock::now();
}

bool ReachabilityBenchmarkRunner::step() noexcept {
    if (impl_ == nullptr || !impl_->job.active()) {
        if (impl_ != nullptr) {
            impl_->freezeTimerIfNeeded();
        }
        return true;
    }

    const bool finished = impl_->job.step();
    if (finished) {
        impl_->freezeTimerIfNeeded();
    }
    return finished;
}

void ReachabilityBenchmarkRunner::cancel() noexcept {
    if (impl_ == nullptr) {
        return;
    }

    impl_->job.cancel();
    impl_->freezeTimerIfNeeded();
}

void ReachabilityBenchmarkRunner::requestSkipCurrentMethod() noexcept {
    if (impl_ != nullptr) {
        impl_->job.requestSkipCurrentMethod();
    }
}

void ReachabilityBenchmarkRunner::reapWorker() noexcept {
    // Kept for API compatibility; benchmarks run on the caller thread now.
}

bool ReachabilityBenchmarkRunner::workerRunning() const noexcept {
    return active();
}

bool ReachabilityBenchmarkRunner::active() const noexcept {
    return impl_ != nullptr && impl_->job.active();
}

double ReachabilityBenchmarkRunner::elapsedSeconds() const noexcept {
    if (impl_ == nullptr || impl_->started_at.time_since_epoch().count() == 0) {
        return 0.0;
    }

    const std::chrono::steady_clock::time_point end_time =
        impl_->timer_frozen ? impl_->ended_at : std::chrono::steady_clock::now();
    const auto elapsed = end_time - impl_->started_at;
    return std::chrono::duration<double>(elapsed).count();
}

const ReachabilityBenchmarkReport& ReachabilityBenchmarkRunner::report() const noexcept {
    static const ReachabilityBenchmarkReport kEmpty{};
    if (impl_ == nullptr) {
        return kEmpty;
    }

    return impl_->job.report();
}

ReachabilityBenchmarkProgress ReachabilityBenchmarkRunner::progress() const noexcept {
    if (impl_ == nullptr) {
        return ReachabilityBenchmarkProgress{};
    }

    return impl_->job.progress();
}

}  // namespace hbrick
