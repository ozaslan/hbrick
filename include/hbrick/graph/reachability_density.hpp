/**
 * @file reachability_density.hpp
 * @ingroup hbrick_graph
 * @brief Sampled estimation of average forward reachability density on directed graphs.
 *
 * Implements a Monte Carlo estimator of the ordered reachable-pair fraction over a
 * caller-supplied vertex universe. Each sample runs a forward BFS reachable-count
 * query from a distinct source chosen by partial Fisher–Yates shuffle at job start. Serial and
 * parallel execution share the same source schedule and produce identical estimates
 * for the same configuration.
 *
 * @see docs/reachability_density_algorithm.md for a formal algorithm specification.
 */

#pragma once

#include <cstdint>
#include <random>
#include <span>
#include <vector>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

/**
 * @brief How many BFS source samples to run before finalizing an estimate.
 * @ingroup hbrick_graph
 */
enum class ReachabilityDensitySampleMode : uint8_t {
    /**
     * @brief Run exactly @c max_samples distinct sources (or every universe vertex).
     * @ingroup hbrick_graph
     *
     * The job ends when @c completed reaches @c source_count. No early stopping.
     */
    FixedSamples,
    /**
     * @brief Stop early once density and its standard error stabilize.
     * @ingroup hbrick_graph
     *
     * After a minimum of 64 samples, convergence is checked every 32 samples.
     * @c max_samples remains a hard upper cap. The user may also call
     * @ref ReachabilityDensityEstimator::stop to accept a partial estimate.
     */
    AutoStopWhenStable,
};

/**
 * @brief Parameters for @ref ReachabilityDensityEstimator.
 * @ingroup hbrick_graph
 */
struct ReachabilityDensityConfig {
    /**
     * @brief Maximum distinct BFS sources, or exact universe size when smaller.
     * @ingroup hbrick_graph
     *
     * When @c universe.size() <= @c max_samples, every universe vertex is used
     * exactly once (exhaustive mode).
     */
    uint32_t max_samples = 512U;

    /**
     * @brief Stopping policy for the sample loop.
     * @ingroup hbrick_graph
     */
    ReachabilityDensitySampleMode sample_mode =
        ReachabilityDensitySampleMode::AutoStopWhenStable;

    /**
     * @brief Seed for the partial Fisher–Yates shuffle at
     *        @ref ReachabilityDensityEstimator::begin.
     * @ingroup hbrick_graph
     *
     * Determines the distinct source order for a given universe and @c max_samples.
     */
    uint64_t rng_seed = 0x9E3779B97F4A7C15ULL;

    /**
     * @brief Worker threads for parallel BFS sampling.
     * @ingroup hbrick_graph
     *
     * @c 1 runs serially on the calling thread. @c 0 selects
     * @c std::thread::hardware_concurrency() (at least @c 1, capped at @c 64).
     * Each worker receives a distinct pre-shuffled source index; sources are
     * never reused within one estimate.
     */
    uint32_t num_threads = 1U;
};

/**
 * @brief Result of a reachable-pair density estimate.
 * @ingroup hbrick_graph
 *
 * Density is the average fraction of universe vertices reachable from distinct
 * universe sources (self included): an unbiased estimate of the ordered
 * reachable-pair fraction over the universe.
 */
struct ReachabilityDensityEstimate {
    /** @brief @c true when at least one BFS sample has been aggregated. @ingroup hbrick_graph */
    bool valid = false;

    /**
     * @brief Sample mean of per-source reachable fractions.
     * @ingroup hbrick_graph
     */
    float density = 0.0F;

    /**
     * @brief Standard error of @c density over completed samples.
     * @ingroup hbrick_graph
     *
     * Set to @c 0 when the estimate is exact (exhaustive universe, no early stop).
     */
    float std_error = 0.0F;

    /** @brief Number of BFS sources aggregated into @c density. @ingroup hbrick_graph */
    uint32_t samples = 0U;
};

/**
 * @brief Incremental reachable-pair density estimator.
 * @ingroup hbrick_graph
 *
 * Cold-path analysis helper (not a hot traversal primitive). May allocate
 * scratch pools and worker threads when @c num_threads != 1.
 *
 * Typical usage:
 * @code
 * ReachabilityDensityConfig config;
 * config.max_samples = 512U;
 * config.num_threads = 0U;  // hardware concurrency
 * ReachabilityDensityEstimator estimator;
 * estimator.begin(graph, universe, config);
 * while (estimator.active()) {
 *     estimator.stepParallel(graph);
 * }
 * ReachabilityDensityEstimate result = estimator.result();
 * @endcode
 */
class ReachabilityDensityEstimator {
public:
    /**
     * @brief Starts a new estimate over @p universe vertices.
     * @ingroup hbrick_graph
     *
     * Copies @p universe into internal storage and partially shuffles the first
     * @c min(max_samples, universe.size()) entries. Sources are drawn without
     * replacement from that prefix. The per-sample denominator is
     * @c universe.size(). When the universe is no larger than @p config.max_samples,
     * every universe vertex is used exactly once. Parallel workers always take the
     * next unused slots from this shuffled prefix.
     *
     * Resets any prior job via @ref ReachabilityDensityEstimator::cancel. When
     * @p universe is empty, the estimator remains inactive.
     *
     * @param graph Directed graph to analyze (read-only during sampling).
     * @param universe Vertex indices defining sampling and the denominator.
     * @param config Sample cap, stopping mode, RNG seed, and thread count.
     */
    void begin(
        const CsrGraph& graph,
        std::span<const uint32_t> universe,
        const ReachabilityDensityConfig& config
    );

    /**
     * @brief Runs one BFS source sample on the calling thread.
     * @ingroup hbrick_graph
     *
     * Processes one distinct source in an internal batch of size @c 1. Uses
     * @c scratch_ when @c effective_threads_ == 1.
     *
     * @param graph Directed graph passed to @ref ReachabilityDensityEstimator::begin.
     * @return @c true when the job finished (cap reached, auto-stop, or inactive).
     *         @c false when more samples remain.
     */
    bool step(const CsrGraph& graph);

    /**
     * @brief Runs up to @p batch_size distinct BFS samples, possibly in parallel.
     * @ingroup hbrick_graph
     *
     * Each sample uses the next unused entry from the pre-shuffled source list
     * (@c universe_[completed_], @c universe_[completed_ + 1], …). Workers never
     * share a source within the batch or across prior batches. When
     * @p batch_size is @c 0, defaults to @ref ReachabilityDensityEstimator::num_threads.
     *
     * When @c effective_threads_ > 1, launches up to one wave of
     * @c std::thread workers per batch, each with its own @c scratch_pool_ entry.
     *
     * @param graph Directed graph passed to @ref ReachabilityDensityEstimator::begin.
     * @param batch_size Maximum samples to run in this call (@c 0 = default).
     * @return @c true when the job finished; @c false when more samples remain.
     */
    bool stepParallel(const CsrGraph& graph, uint32_t batch_size = 0U);

    /**
     * @brief Aborts an active job without updating the stored result.
     * @ingroup hbrick_graph
     *
     * Clears internal state including @c universe_ and @c scratch_pool_.
     * After @ref ReachabilityDensityEstimator::cancel,
     * @ref ReachabilityDensityEstimator::result returns a default-constructed estimate.
     */
    void cancel() noexcept;

    /**
     * @brief Stops an active job and keeps the estimate from completed samples.
     * @ingroup hbrick_graph
     *
     * Finalises with @c stopped_early = true when at least one sample completed.
     * No-op when inactive or when @ref completed is zero.
     */
    void stop() noexcept;

    /**
     * @brief Runs @ref ReachabilityDensityEstimator::begin and drains samples until completion.
     * @ingroup hbrick_graph
     *
     * Uses @ref stepParallel when @c config.num_threads != 1, otherwise
     * @ref step.
     *
     * @param graph Directed graph to analyze.
     * @param universe Vertex indices defining sampling and the denominator.
     * @param config Sample cap, stopping mode, RNG seed, and thread count.
     * @return Final @ref ReachabilityDensityEstimate after the job completes.
     */
    [[nodiscard]] ReachabilityDensityEstimate estimate(
        const CsrGraph& graph,
        std::span<const uint32_t> universe,
        const ReachabilityDensityConfig& config
    );

    /**
     * @brief Returns whether a job is in progress.
     * @ingroup hbrick_graph
     * @return @c true after a successful @ref ReachabilityDensityEstimator::begin until
     *         finish, @ref ReachabilityDensityEstimator::stop, or
     *         @ref ReachabilityDensityEstimator::cancel.
     */
    [[nodiscard]] bool active() const noexcept { return active_; }

    /**
     * @brief Returns how many BFS sources have been aggregated so far.
     * @ingroup hbrick_graph
     * @return Count of completed samples in the current or last finalised job.
     */
    [[nodiscard]] uint32_t completed() const noexcept { return completed_; }

    /**
     * @brief Returns the planned number of samples for this job.
     * @ingroup hbrick_graph
     * @return @c universe.size() in exhaustive mode, otherwise @c max_samples.
     */
    [[nodiscard]] uint32_t source_count() const noexcept { return source_count_; }

    /**
     * @brief Returns whether every universe vertex will be sampled.
     * @ingroup hbrick_graph
     * @return @c true when @c universe.size() <= @c max_samples at
     *         @ref ReachabilityDensityEstimator::begin.
     */
    [[nodiscard]] bool exhaustive() const noexcept { return exhaustive_; }

    /**
     * @brief Returns the resolved worker thread count for this job.
     * @ingroup hbrick_graph
     * @return @c 1 for serial mode; otherwise hardware concurrency or the configured cap.
     */
    [[nodiscard]] uint32_t num_threads() const noexcept { return effective_threads_; }

    /**
     * @brief Returns the sample-mode from the active job configuration.
     * @ingroup hbrick_graph
     * @return Copy of @ref ReachabilityDensityConfig::sample_mode for this job.
     */
    [[nodiscard]] ReachabilityDensitySampleMode sample_mode() const noexcept {
        return config_.sample_mode;
    }

    /**
     * @brief Returns the last finalised estimate.
     * @ingroup hbrick_graph
     *
     * Valid after the job finishes or after @ref ReachabilityDensityEstimator::stop.
     * During an active job, prefer @ref ReachabilityDensityEstimator::snapshot for a
     * running mean.
     *
     * @return Last estimate produced when the job finished or was stopped early.
     */
    [[nodiscard]] ReachabilityDensityEstimate result() const noexcept {
        return result_;
    }

    /**
     * @brief Returns a running mean and standard error from completed samples.
     * @ingroup hbrick_graph
     *
     * Does not mark the estimate as exact when the job is still active.
     *
     * @return Estimate from @c completed samples, or invalid when @c completed == 0.
     */
    [[nodiscard]] ReachabilityDensityEstimate snapshot() const noexcept;

    /**
     * @brief Returns the shuffled universe used for source selection.
     * @ingroup hbrick_graph
     *
     * The first @ref ReachabilityDensityEstimator::source_count entries are the
     * distinct sources for this job.
     *
     * @return Read-only view of the internal shuffled vertex list.
     */
    [[nodiscard]] std::span<const uint32_t> universe() const noexcept {
        return universe_;
    }

private:
    /**
     * @brief Writes @ref result_ from running accumulators.
     * @param stopped_early When @c true, the job ended before @ref source_count.
     */
    void finalize(bool stopped_early) noexcept;

    /**
     * @brief Tests auto-stop convergence for @ref AutoStopWhenStable mode.
     * @return @c true when stable-round criteria are satisfied.
     */
    [[nodiscard]] bool checkAutoConvergence() noexcept;

    /**
     * @brief Runs up to @p batch_size BFS samples and updates accumulators.
     *
     * Dispatches to the serial or parallel path based on @c effective_threads_.
     * Invokes @ref finalize when the sample cap or auto-stop rule triggers.
     *
     * @param graph Directed graph passed to @ref ReachabilityDensityEstimator::begin.
     * @param batch_size Number of distinct sources to process in this call.
     * @return @c true when the job finished; @c false when more samples remain.
     */
    [[nodiscard]] bool runSampleBatch(const CsrGraph& graph, uint32_t batch_size);

    /** @brief Active job configuration copy. */
    ReachabilityDensityConfig config_{};

    /** @brief Serial-mode BFS workspace (one sample per @ref step call). */
    GraphSearchScratch scratch_;

    /** @brief Per-worker BFS workspace when @c effective_threads_ > 1. */
    std::vector<GraphSearchScratch> scratch_pool_;

    /** @brief Last finalised estimate. */
    ReachabilityDensityEstimate result_{};

    /** @brief Whether @ref ReachabilityDensityEstimator::begin started a job that has not
     *         finished or been cancelled. */
    bool active_ = false;

    /** @brief @c true when all universe vertices are sampled. */
    bool exhaustive_ = false;

    /** @brief Number of BFS samples completed in the current job. */
    uint32_t completed_ = 0U;

    /** @brief Planned sample count (@c |U| or @c max_samples). */
    uint32_t source_count_ = 0U;

    /** @brief Resolved thread count from @ref ReachabilityDensityConfig::num_threads. */
    uint32_t effective_threads_ = 1U;

    /** @brief Denominator for per-source fractions (@c universe.size() as double). */
    double universe_size_ = 0.0;

    /** @brief Running sum of per-source fractions. */
    double sum_ = 0.0;

    /** @brief Running sum of squared per-source fractions. */
    double sum_squares_ = 0.0;

    /** @brief Density at the last auto-stop checkpoint. */
    float checkpoint_density_ = 0.0F;

    /** @brief Standard error at the last auto-stop checkpoint. */
    float checkpoint_std_error_ = 0.0F;

    /** @brief Whether the first auto-stop checkpoint has been recorded. */
    bool checkpoint_initialized_ = false;

    /** @brief Consecutive stable auto-stop checks satisfied. */
    uint32_t stable_rounds_ = 0U;

    /** @brief Shuffled copy of the caller universe; first @c source_count_ are sources. */
    std::vector<uint32_t> universe_;

    /** @brief RNG for partial Fisher–Yates at @ref ReachabilityDensityEstimator::begin. */
    std::mt19937_64 rng_{0x9E3779B97F4A7C15ULL};
};

}  // namespace hbrick
