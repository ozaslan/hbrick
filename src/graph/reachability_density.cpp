#include "hbrick/graph/reachability_density.hpp"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

#include "hbrick/graph/bfs.hpp"

namespace hbrick {

namespace {

constexpr uint32_t kAutoMinSamples = 64U;
constexpr uint32_t kAutoCheckInterval = 32U;
constexpr uint32_t kAutoStableRoundsRequired = 3U;
constexpr uint32_t kMaxParallelThreads = 64U;

[[nodiscard]] bool metricsStable(
    const double prev_density,
    const double prev_std_error,
    const double density,
    const double std_error
) noexcept {
    const double density_tol = std::max(
        1e-6,
        0.001 * std::max(prev_density, density)
    );
    const double sigma_tol = std::max(
        1e-7,
        0.03 * std::max(prev_std_error, std_error)
    );
    return std::abs(density - prev_density) <= density_tol
        && std::abs(std_error - prev_std_error) <= sigma_tol;
}

[[nodiscard]] ReachabilityDensityEstimate computeSnapshot(
    const uint32_t completed,
    const uint32_t source_count,
    const bool exhaustive,
    const bool stopped_early,
    const double sum,
    const double sum_squares
) noexcept {
    ReachabilityDensityEstimate estimate;
    if (completed == 0U) {
        return estimate;
    }

    const double n = static_cast<double>(completed);
    const double mean = sum / n;
    const double variance = std::max(0.0, sum_squares / n - mean * mean);
    const bool exact = exhaustive && !stopped_early && completed >= source_count;
    const double std_error = exact ? 0.0 : std::sqrt(variance / n);

    estimate.valid = true;
    estimate.density = static_cast<float>(mean);
    estimate.std_error = static_cast<float>(std_error);
    estimate.samples = completed;
    return estimate;
}

void partialShuffleUniverse(
    std::vector<uint32_t>& values,
    const uint32_t sample_count,
    std::mt19937_64& rng
) {
    const std::size_t universe_size = values.size();
    const std::size_t shuffle_count = std::min(
        static_cast<std::size_t>(sample_count),
        universe_size
    );
    std::uniform_int_distribution<std::size_t> pick;
    for (std::size_t index = 0; index < shuffle_count; ++index) {
        pick.param(
            std::uniform_int_distribution<std::size_t>::param_type(
                index,
                universe_size - 1U
            )
        );
        const std::size_t swap_index = pick(rng);
        std::swap(values[index], values[swap_index]);
    }
}

[[nodiscard]] uint32_t resolveEffectiveThreads(const uint32_t requested) {
    if (requested == 1U) {
        return 1U;
    }

    uint32_t threads = requested;
    if (threads == 0U) {
        const unsigned int hardware = std::thread::hardware_concurrency();
        threads = hardware > 0U ? static_cast<uint32_t>(hardware) : 1U;
    }
    return std::max(1U, std::min(threads, kMaxParallelThreads));
}

}  // namespace

void ReachabilityDensityEstimator::begin(
    const CsrGraph& graph,
    const std::span<const uint32_t> universe,
    const ReachabilityDensityConfig& config
) {
    cancel();
    result_ = {};
    config_ = config;

    if (universe.empty()) {
        return;
    }

    effective_threads_ = resolveEffectiveThreads(config_.num_threads);
    scratch_.resetForGraph(graph.numVertices());
    scratch_pool_.clear();
    if (effective_threads_ > 1U) {
        scratch_pool_.reserve(effective_threads_);
        for (uint32_t worker = 0; worker < effective_threads_; ++worker) {
            scratch_pool_.emplace_back(graph.numVertices());
        }
    }

    universe_.assign(universe.begin(), universe.end());

    exhaustive_ = universe_.size() <= static_cast<std::size_t>(config_.max_samples);
    source_count_ = exhaustive_
        ? static_cast<uint32_t>(universe_.size())
        : config_.max_samples;
    universe_size_ = static_cast<double>(universe_.size());

    rng_.seed(config_.rng_seed);
    partialShuffleUniverse(universe_, source_count_, rng_);

    completed_ = 0U;
    sum_ = 0.0;
    sum_squares_ = 0.0;
    checkpoint_density_ = 0.0F;
    checkpoint_std_error_ = 0.0F;
    checkpoint_initialized_ = false;
    stable_rounds_ = 0U;
    active_ = true;
}

bool ReachabilityDensityEstimator::runSampleBatch(
    const CsrGraph& graph,
    const uint32_t batch_size
) {
    if (!active_) {
        return true;
    }

    const uint32_t remaining = source_count_ - completed_;
    if (remaining == 0U) {
        finalize(false);
        active_ = false;
        return true;
    }

    const uint32_t count = std::min(batch_size, remaining);
    std::vector<double> fractions(count);

    if (effective_threads_ <= 1U || count == 1U) {
        for (uint32_t offset = 0; offset < count; ++offset) {
            const uint32_t source = universe_[completed_ + offset];
            const uint32_t reached = Bfs::reachableCount(graph, source, scratch_);
            fractions[offset] = static_cast<double>(reached) / universe_size_;
        }
    } else {
        uint32_t offset = 0U;
        while (offset < count) {
            const uint32_t wave = std::min(count - offset, effective_threads_);
            std::vector<std::thread> workers;
            workers.reserve(wave);
            for (uint32_t worker = 0; worker < wave; ++worker) {
                const uint32_t sample_index = completed_ + offset + worker;
                const uint32_t source = universe_[sample_index];
                workers.emplace_back([&, worker, source]() {
                    const uint32_t reached = Bfs::reachableCount(
                        graph,
                        source,
                        scratch_pool_[worker]
                    );
                    fractions[offset + worker] =
                        static_cast<double>(reached) / universe_size_;
                });
            }
            for (std::thread& worker : workers) {
                worker.join();
            }
            offset += wave;
        }
    }

    for (uint32_t offset = 0; offset < count; ++offset) {
        sum_ += fractions[offset];
        sum_squares_ += fractions[offset] * fractions[offset];
    }
    completed_ += count;

    if (checkAutoConvergence()) {
        finalize(true);
        active_ = false;
        return true;
    }

    if (completed_ >= source_count_) {
        finalize(false);
        active_ = false;
        return true;
    }
    return false;
}

bool ReachabilityDensityEstimator::step(const CsrGraph& graph) {
    return runSampleBatch(graph, 1U);
}

bool ReachabilityDensityEstimator::stepParallel(
    const CsrGraph& graph,
    const uint32_t batch_size
) {
    const uint32_t effective_batch =
        batch_size == 0U ? effective_threads_ : batch_size;
    return runSampleBatch(graph, std::max(1U, effective_batch));
}

void ReachabilityDensityEstimator::cancel() noexcept {
    active_ = false;
    completed_ = 0U;
    source_count_ = 0U;
    effective_threads_ = 1U;
    exhaustive_ = false;
    universe_size_ = 0.0;
    sum_ = 0.0;
    sum_squares_ = 0.0;
    checkpoint_initialized_ = false;
    stable_rounds_ = 0U;
    universe_.clear();
    scratch_pool_.clear();
}

void ReachabilityDensityEstimator::stop() noexcept {
    if (!active_) {
        return;
    }
    if (completed_ > 0U) {
        finalize(true);
    }
    active_ = false;
}

ReachabilityDensityEstimate ReachabilityDensityEstimator::estimate(
    const CsrGraph& graph,
    const std::span<const uint32_t> universe,
    const ReachabilityDensityConfig& config
) {
    begin(graph, universe, config);
    while (active_) {
        if (effective_threads_ > 1U) {
            stepParallel(graph);
        } else {
            step(graph);
        }
    }
    return result_;
}

ReachabilityDensityEstimate ReachabilityDensityEstimator::snapshot() const noexcept {
    return computeSnapshot(
        completed_,
        source_count_,
        exhaustive_,
        false,
        sum_,
        sum_squares_
    );
}

void ReachabilityDensityEstimator::finalize(const bool stopped_early) noexcept {
    if (completed_ == 0U) {
        return;
    }

    result_ = computeSnapshot(
        completed_,
        source_count_,
        exhaustive_,
        stopped_early,
        sum_,
        sum_squares_
    );
}

bool ReachabilityDensityEstimator::checkAutoConvergence() noexcept {
    if (config_.sample_mode != ReachabilityDensitySampleMode::AutoStopWhenStable) {
        return false;
    }
    if (completed_ < kAutoMinSamples) {
        return false;
    }
    if (completed_ % kAutoCheckInterval != 0U) {
        return false;
    }

    const ReachabilityDensityEstimate current = snapshot();
    const double density = static_cast<double>(current.density);
    const double std_error = static_cast<double>(current.std_error);

    if (!checkpoint_initialized_) {
        checkpoint_density_ = current.density;
        checkpoint_std_error_ = current.std_error;
        checkpoint_initialized_ = true;
        return false;
    }

    const double prev_density = static_cast<double>(checkpoint_density_);
    const double prev_std_error = static_cast<double>(checkpoint_std_error_);

    if (metricsStable(prev_density, prev_std_error, density, std_error)) {
        ++stable_rounds_;
    } else {
        stable_rounds_ = 0U;
    }

    checkpoint_density_ = current.density;
    checkpoint_std_error_ = current.std_error;
    return stable_rounds_ >= kAutoStableRoundsRequired;
}

}  // namespace hbrick
