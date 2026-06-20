#include "hbrick/bench/reachability_benchmark_runner.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

namespace hbrick {

struct ReachabilityBenchmarkRunner::Impl {
    ReachabilityBenchmarkJob job;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> worker_running{false};
    std::unique_ptr<std::thread> worker;
    std::chrono::steady_clock::time_point started_at{};
    std::chrono::steady_clock::time_point ended_at{};
    bool timer_frozen = false;

    void freezeTimerIfNeeded() noexcept {
        if (!timer_frozen && started_at.time_since_epoch().count() != 0) {
            ended_at = std::chrono::steady_clock::now();
            timer_frozen = true;
        }
    }

    void detachWorkerIfJoinable() noexcept {
        if (worker != nullptr && worker->joinable()) {
            worker->detach();
        }
        worker.reset();
        worker_running.store(false, std::memory_order_release);
    }
};

ReachabilityBenchmarkRunner::ReachabilityBenchmarkRunner()
    : impl_(std::make_unique<Impl>()) {}

ReachabilityBenchmarkRunner::~ReachabilityBenchmarkRunner() {
    cancel();
    reapWorker();
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
    reapWorker();

    impl_->stop_requested.store(false, std::memory_order_release);
    impl_->timer_frozen = false;
    impl_->started_at = {};
    impl_->ended_at = {};

    impl_->job.begin(graph, query_universe, std::move(config));
    if (!impl_->job.active()) {
        return;
    }

    impl_->started_at = std::chrono::steady_clock::now();
    impl_->worker_running.store(true, std::memory_order_release);

    ReachabilityBenchmarkJob* job = &impl_->job;
    Impl* runner_impl = impl_.get();
    impl_->worker = std::make_unique<std::thread>([job, runner_impl]() {
        while (!runner_impl->stop_requested.load(std::memory_order_acquire)) {
            if (!job->active()) {
                break;
            }
            if (job->step()) {
                break;
            }
        }
        runner_impl->worker_running.store(false, std::memory_order_release);
    });
}

void ReachabilityBenchmarkRunner::begin(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const std::span<const uint32_t> query_universe,
    ReachabilityBenchmarkConfig config
) {
    cancel();
    reapWorker();

    impl_->stop_requested.store(false, std::memory_order_release);
    impl_->timer_frozen = false;
    impl_->started_at = {};
    impl_->ended_at = {};

    impl_->job.begin(graph, layout, query_universe, std::move(config));
    if (!impl_->job.active()) {
        return;
    }

    impl_->started_at = std::chrono::steady_clock::now();
    impl_->worker_running.store(true, std::memory_order_release);

    ReachabilityBenchmarkJob* job = &impl_->job;
    Impl* runner_impl = impl_.get();
    impl_->worker = std::make_unique<std::thread>([job, runner_impl]() {
        while (!runner_impl->stop_requested.load(std::memory_order_acquire)) {
            if (!job->active()) {
                break;
            }
            if (job->step()) {
                break;
            }
        }
        runner_impl->worker_running.store(false, std::memory_order_release);
    });
}

void ReachabilityBenchmarkRunner::cancel() noexcept {
    if (impl_ == nullptr) {
        return;
    }

    impl_->stop_requested.store(true, std::memory_order_release);
    impl_->job.cancel();
    impl_->freezeTimerIfNeeded();
    impl_->detachWorkerIfJoinable();
}

void ReachabilityBenchmarkRunner::requestSkipCurrentMethod() noexcept {
    if (impl_ != nullptr) {
        impl_->job.requestSkipCurrentMethod();
    }
}

void ReachabilityBenchmarkRunner::reapWorker() noexcept {
    if (impl_ == nullptr || impl_->worker == nullptr) {
        return;
    }

    if (impl_->worker_running.load(std::memory_order_acquire)) {
        return;
    }

    impl_->freezeTimerIfNeeded();
    if (impl_->worker->joinable()) {
        impl_->worker->join();
    }
    impl_->worker.reset();
}

bool ReachabilityBenchmarkRunner::workerRunning() const noexcept {
    return impl_ != nullptr
        && impl_->worker_running.load(std::memory_order_acquire);
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
