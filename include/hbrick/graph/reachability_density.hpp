/**
 * @file reachability_density.hpp
 * @ingroup hbrick_graph
 * @brief Sampled estimation of average forward reachability density on directed graphs.
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
    /** @brief Run exactly @c max_samples random sources (or every universe vertex). */
    FixedSamples,
    /**
     * @brief Stop early once density and its standard error stabilize.
     *
     * @c max_samples is an upper cap; the user may also call @ref
     * ReachabilityDensityEstimator::stop to accept a partial estimate.
     */
    AutoStopWhenStable,
};

/**
 * @brief Parameters for @ref ReachabilityDensityEstimator.
 * @ingroup hbrick_graph
 */
struct ReachabilityDensityConfig {
    /** @brief Maximum random sources, or exact universe size when smaller. */
    uint32_t max_samples = 512U;
    ReachabilityDensitySampleMode sample_mode =
        ReachabilityDensitySampleMode::AutoStopWhenStable;
    /** @brief Seed for random source selection when sampling. */
    uint64_t rng_seed = 0x9E3779B97F4A7C15ULL;
    /**
     * @brief Worker threads for parallel BFS sampling.
     *
     * @c 1 runs serially. @c 0 selects @c std::thread::hardware_concurrency()
     * (at least @c 1). Each worker receives a distinct pre-shuffled source
     * index; sources are never reused within one estimate.
     */
    uint32_t num_threads = 1U;
};

/**
 * @brief Result of a reachable-pair density estimate.
 * @ingroup hbrick_graph
 *
 * Density is the average fraction of @c universe vertices reachable from a
 * uniformly chosen distinct universe sources (self included). This is an
 * unbiased estimate of the ordered reachable-pair fraction over the universe.
 */
struct ReachabilityDensityEstimate {
    bool valid = false;
    float density = 0.0F;
    float std_error = 0.0F;
    uint32_t samples = 0U;
};

/**
 * @brief Incremental reachable-pair density estimator (one BFS sample per step).
 * @ingroup hbrick_graph
 */
class ReachabilityDensityEstimator {
public:
    /**
     * @brief Starts a new estimate over @p universe vertices.
     *
     * Sources are drawn without replacement from @p universe via a partial
     * Fisher–Yates shuffle of the first @c min(max_samples, universe.size())
     * slots. The denominator for each sample is @c universe.size(). When the
     * universe is no larger than @p config.max_samples, every universe vertex
     * is used exactly once. Parallel workers always take the next unused slots
     * from this shuffled prefix.
     *
     * @param graph Directed graph to analyze.
     * @param universe Vertex indices that define sampling and the denominator.
     * @param config Sample cap, stopping mode, and RNG seed.
     */
    void begin(
        const CsrGraph& graph,
        std::span<const uint32_t> universe,
        const ReachabilityDensityConfig& config
    );

    /**
     * @brief Runs one BFS source sample on the calling thread.
     *
     * @param graph Directed graph passed to @ref begin.
     * @return @c true when the job finished (success, auto-stop, or cap hit).
     */
    bool step(const CsrGraph& graph);

    /**
     * @brief Runs up to @p batch_size distinct BFS samples in parallel.
     *
     * Each sample uses the next unused entry from the pre-shuffled source list
     * (@c universe[completed], @c universe[completed + 1], …). Workers never
     * share a source within the batch or across prior batches. When
     * @p batch_size is @c 0, defaults to @ref num_threads().
     *
     * @param graph Directed graph passed to @ref begin.
     * @param batch_size Maximum samples to run in this call (@c 0 = default).
     * @return @c true when the job finished (success, auto-stop, or cap hit).
     */
    bool stepParallel(const CsrGraph& graph, uint32_t batch_size = 0U);

    /** @brief Aborts an active job without updating the stored result. */
    void cancel() noexcept;

    /**
     * @brief Stops an active job and keeps the estimate from completed samples.
     *
     * No-op when inactive or when no samples have finished yet.
     */
    void stop() noexcept;

    /**
     * @brief Runs @ref begin and drains samples until completion.
     *
     * Uses @ref stepParallel when @c config.num_threads != 1, otherwise
     * @ref step.
     */
    [[nodiscard]] ReachabilityDensityEstimate estimate(
        const CsrGraph& graph,
        std::span<const uint32_t> universe,
        const ReachabilityDensityConfig& config
    );

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] uint32_t completed() const noexcept { return completed_; }
    [[nodiscard]] uint32_t source_count() const noexcept { return source_count_; }
    [[nodiscard]] bool exhaustive() const noexcept { return exhaustive_; }
    [[nodiscard]] uint32_t num_threads() const noexcept { return effective_threads_; }
    [[nodiscard]] ReachabilityDensitySampleMode sample_mode() const noexcept {
        return config_.sample_mode;
    }
    [[nodiscard]] ReachabilityDensityEstimate result() const noexcept {
        return result_;
    }
    [[nodiscard]] ReachabilityDensityEstimate snapshot() const noexcept;
    [[nodiscard]] std::span<const uint32_t> universe() const noexcept {
        return universe_;
    }

private:
    void finalize(bool stopped_early) noexcept;
    [[nodiscard]] bool checkAutoConvergence() noexcept;
    [[nodiscard]] bool runSampleBatch(const CsrGraph& graph, uint32_t batch_size);

    ReachabilityDensityConfig config_{};
    GraphSearchScratch scratch_;
    std::vector<GraphSearchScratch> scratch_pool_;
    ReachabilityDensityEstimate result_{};

    bool active_ = false;
    bool exhaustive_ = false;
    uint32_t completed_ = 0U;
    uint32_t source_count_ = 0U;
    uint32_t effective_threads_ = 1U;
    double universe_size_ = 0.0;
    double sum_ = 0.0;
    double sum_squares_ = 0.0;
    float checkpoint_density_ = 0.0F;
    float checkpoint_std_error_ = 0.0F;
    bool checkpoint_initialized_ = false;
    uint32_t stable_rounds_ = 0U;

    std::vector<uint32_t> universe_;
    std::mt19937_64 rng_{0x9E3779B97F4A7C15ULL};
};

}  // namespace hbrick
