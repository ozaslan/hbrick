/**
 * @file reachability_benchmark.hpp
 * @ingroup hbrick_bench
 * @brief Reachability baseline performance benchmarking for fixed query workloads.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/baselines/grail_baseline.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/tile/group_size.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/**
 * @brief Identifiers for built-in reachability baseline methods.
 * @ingroup hbrick_bench
 */
enum class ReachabilityBaselineId : uint8_t {
    CsrBfs = 0,
    CsrDfs,
    SccDagSearch,
    SccDagClosure,
    FullClosure,
    TwoHop,
    Grail,
    BrickSearch,
    BrickClosure,
    HBrick,
};

/**
 * @brief Human-readable name for @p id.
 * @ingroup hbrick_bench
 */
[[nodiscard]] const char* reachabilityBaselineName(
    ReachabilityBaselineId id
) noexcept;

/**
 * @brief One directed reachability query pair.
 * @ingroup hbrick_bench
 */
struct ReachabilityQueryPair {
    uint32_t source = 0U;
    uint32_t target = 0U;
};

/**
 * @brief Configuration for @ref ReachabilityBenchmarkJob.
 * @ingroup hbrick_bench
 */
struct ReachabilityBenchmarkConfig {
    /** @brief Maximum index memory allowed during preprocessing. @ingroup hbrick_bench */
    uint64_t max_memory_bytes = 8ULL << 30;
    /** @brief Number of timed query pairs shared by every baseline. @ingroup hbrick_bench */
    uint32_t query_count = 4096U;
    /** @brief Warmup queries per baseline before timing (not recorded). @ingroup hbrick_bench */
    uint32_t warmup_queries = 32U;
    /** @brief Seed for reproducible query-pair generation. @ingroup hbrick_bench */
    uint64_t pair_seed = 0xBEEFCAFEULL;
    /** @brief GRAIL preprocessing parameters. @ingroup hbrick_bench */
    GrailBaselineParams grail_params{};
    /** @brief Random pairs verified against BFS after timing completes. @ingroup hbrick_bench */
    uint32_t correctness_check_count = 256U;
    /** @brief Timed queries executed per @ref ReachabilityBenchmarkJob::step call. @ingroup hbrick_bench */
    uint32_t queries_per_step = 256U;
    /**
     * @brief When @c true, incremental closure preprocess may stop early if projected
     *        total speedup vs CsrBfs drops below @ref closure_min_projected_total_speedup_vs_bfs.
     * @ingroup hbrick_bench
     */
    bool closure_enable_projected_speedup_early_stop = false;
    /** @brief Minimum projected end-to-end speedup vs CsrBfs required to continue closure preprocess. @ingroup hbrick_bench */
    float closure_min_projected_total_speedup_vs_bfs = 0.5F;
    /**
     * @brief Baseline methods to benchmark.
     * @ingroup hbrick_bench
     *
     * When empty, @ref ReachabilityBenchmarkConfig::allMethods() is used.
     */
    std::vector<ReachabilityBaselineId> methods;
    /** @brief Nominal base tile size for BRICK / H-BRICK grid baselines. @ingroup hbrick_bench */
    TileSize brick_tile_size{4U, 4U};
    /**
     * @brief When @c true, Kleene squaring during @ref ReachabilityBaselineId::BrickClosure
     *        preprocess uses multi-threaded boolean matrix squaring.
     * @ingroup hbrick_bench
     */
    bool kleene_use_parallel = false;
    /**
     * @brief Kleene worker count when @ref kleene_use_parallel is @c true.
     * @ingroup hbrick_bench
     *
     * @c 0 selects hardware concurrency (capped).
     */
    uint32_t kleene_num_threads = 0U;
    /** @brief Child-slot grouping for @ref ReachabilityBaselineId::HBrick. @ingroup hbrick_bench */
    GroupSize hbrick_group_size{2U, 2U};
    /**
     * @brief Hierarchy depth for @ref ReachabilityBaselineId::HBrick.
     * @ingroup hbrick_bench
     *
     * Use @ref kHBrickFullDepth to build until one root remains.
     */
    uint32_t hbrick_max_depth = kHBrickFullDepth;

    /** @brief Returns a configuration that benchmarks every built-in baseline. @ingroup hbrick_bench */
    [[nodiscard]] static ReachabilityBenchmarkConfig allMethods() noexcept;
};

/**
 * @brief Aggregated query timing statistics.
 * @ingroup hbrick_bench
 */
struct QueryTimingStatistics {
    uint64_t count = 0U;
    uint64_t total_nanoseconds = 0U;
    uint64_t min_nanoseconds = 0U;
    uint64_t max_nanoseconds = 0U;
    uint64_t median_nanoseconds = 0U;
    uint64_t p95_nanoseconds = 0U;
    double mean_nanoseconds = 0.0;
    double queries_per_second = 0.0;
};

/**
 * @brief Per-baseline benchmark metrics.
 * @ingroup hbrick_bench
 */
struct BaselineBenchmarkMetrics {
    ReachabilityBaselineId method = ReachabilityBaselineId::CsrBfs;
    BaselineStatus status = BaselineStatus::NotRun;
    uint64_t preprocess_nanoseconds = 0U;
    uint64_t estimated_index_bytes = 0U;
    uint64_t preprocess_rss_delta_bytes = 0U;
    QueryTimingStatistics query_stats{};
    uint32_t reachable_count = 0U;
    uint32_t unreachable_count = 0U;
    double reachable_ratio = 0.0;
    uint32_t positive_query_count = 0U;
    uint32_t negative_query_count = 0U;
    double mean_positive_query_nanoseconds = 0.0;
    double mean_negative_query_nanoseconds = 0.0;
    uint32_t grail_tree_hits = 0U;
    uint32_t grail_bfs_fallbacks = 0U;
    uint32_t correctness_checks = 0U;
    uint32_t correctness_mismatches = 0U;
    /** @brief Wall time spent in warmup queries for this method. @ingroup hbrick_bench */
    uint64_t warmup_nanoseconds = 0U;
    /** @brief Wall time spent in correctness-check queries for this method. @ingroup hbrick_bench */
    uint64_t correctness_nanoseconds = 0U;
    /**
     * @brief End-to-end time for this method: preprocess, warmup, and timed queries.
     *        Excludes correctness-check queries.
     * @ingroup hbrick_bench
     */
    uint64_t total_benchmark_nanoseconds = 0U;
    /** @brief Mean timed-query speedup vs @ref ReachabilityBaselineId::CsrBfs. @ingroup hbrick_bench */
    double speedup_vs_bfs = 0.0;
    /** @brief End-to-end speedup vs @ref ReachabilityBaselineId::CsrBfs. @ingroup hbrick_bench */
    double total_speedup_vs_bfs = 0.0;
    /**
     * @brief Latest projected end-to-end speedup vs CsrBfs during closure preprocessing.
     * @ingroup hbrick_bench
     */
    double projected_total_speedup_vs_bfs = 0.0;
    /**
     * @brief Order of the bit matrix used for Warshall preprocessing (|V| or SCC count C).
     * @ingroup hbrick_bench
     */
    uint32_t warshall_matrix_order = 0U;
    /** @brief Completed Floyd-Warshall pivot steps (outer index k). @ingroup hbrick_bench */
    uint32_t warshall_pivots_completed = 0U;
    /** @brief Total Floyd-Warshall pivot steps expected for this method. @ingroup hbrick_bench */
    uint32_t warshall_pivot_total = 0U;
    /** @brief Whether Kleene squaring used parallel workers (BrickClosure). @ingroup hbrick_bench */
    bool kleene_parallel = false;
    /** @brief Effective Kleene worker count when @ref kleene_parallel is @c true. @ingroup hbrick_bench */
    uint32_t kleene_thread_count = 1U;
    /**
     * @brief Human-readable explanation when @ref status is @ref BaselineStatus::SkippedByPolicy.
     * @ingroup hbrick_bench
     */
    std::string policy_skip_detail;
};

/**
 * @brief Live progress snapshot for incremental benchmark jobs.
 * @ingroup hbrick_bench
 */
struct ReachabilityBenchmarkProgress {
    enum class Stage : uint8_t {
        Idle,
        GeneratingPairs,
        Preprocessing,
        WarmingUp,
        Querying,
        CorrectnessCheck,
        Finished,
        Cancelled,
    };

    Stage stage = Stage::Idle;
    ReachabilityBaselineId current_method = ReachabilityBaselineId::CsrBfs;
    uint32_t methods_completed = 0U;
    uint32_t methods_total = 0U;
    uint32_t queries_completed = 0U;
    uint32_t queries_total = 0U;
    /** @brief Completed fine-grained work units across all benchmark phases. @ingroup hbrick_bench */
    uint64_t work_completed = 0U;
    /** @brief Total fine-grained work units for the configured workload. @ingroup hbrick_bench */
    uint64_t work_total = 0U;
    /** @brief Progress within the current method and stage (e.g. queries done). @ingroup hbrick_bench */
    uint32_t stage_work_completed = 0U;
    /** @brief Work expected in the current method and stage. @ingroup hbrick_bench */
    uint32_t stage_work_total = 0U;
    /**
     * @brief Monotonic timestamp when the active preprocess step started.
     * @ingroup hbrick_bench
     *
     * Zero when no preprocess step is currently running.
     */
    uint64_t preprocess_started_ns = 0U;
};

/**
 * @brief Final benchmark report for one graph and query workload.
 * @ingroup hbrick_bench
 */
struct ReachabilityBenchmarkReport {
    bool valid = false;
    uint32_t num_vertices = 0U;
    uint64_t num_edges = 0U;
    uint32_t query_pair_count = 0U;
    uint64_t pair_seed = 0U;
    double reference_bfs_mean_query_nanoseconds = 0.0;
    uint64_t reference_bfs_total_benchmark_nanoseconds = 0U;
    std::vector<BaselineBenchmarkMetrics> methods;
};

/**
 * @brief Incremental reachability baseline benchmark runner.
 * @ingroup hbrick_bench
 *
 * Generates one fixed query workload shared by every selected baseline,
 * runs each method end-to-end (preprocess, warmup, timed queries, correctness),
 * and produces a @ref ReachabilityBenchmarkReport.
 */
class ReachabilityBenchmarkJob {
public:
    ReachabilityBenchmarkJob();
    ~ReachabilityBenchmarkJob();

    ReachabilityBenchmarkJob(const ReachabilityBenchmarkJob&) = delete;
    ReachabilityBenchmarkJob& operator=(const ReachabilityBenchmarkJob&) = delete;
    ReachabilityBenchmarkJob(ReachabilityBenchmarkJob&&) noexcept;
    ReachabilityBenchmarkJob& operator=(ReachabilityBenchmarkJob&&) noexcept;

    /**
     * @brief Starts a benchmark on @p graph using vertices from @p query_universe.
     * @ingroup hbrick_bench
     */
    void begin(
        const CsrGraph& graph,
        std::span<const uint32_t> query_universe,
        ReachabilityBenchmarkConfig config
    );

    /**
     * @brief Starts a benchmark on a grid-embedded graph with maze passability.
     * @ingroup hbrick_bench
     *
     * Required for @ref ReachabilityBaselineId::BrickSearch, @ref BrickClosure,
     * and @ref HBrick.
     */
    void begin(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        std::span<const uint32_t> query_universe,
        ReachabilityBenchmarkConfig config
    );

    /** @brief Returns @c true while a job is active. @ingroup hbrick_bench */
    [[nodiscard]] bool active() const noexcept;

    /** @brief Aborts the active job without producing a valid report. @ingroup hbrick_bench */
    void cancel() noexcept;

    /**
     * @brief Skips the active method's remaining work (e.g. aborts closure preprocessing).
     * @ingroup hbrick_bench
     *
     * Safe to call from the UI thread while @ref step runs on a worker thread.
     */
    void requestSkipCurrentMethod() noexcept;

    /**
     * @brief Advances the job by up to @p config.queries_per_step timed queries.
     * @ingroup hbrick_bench
     *
     * @return @c true when the job finished normally or was cancelled.
     */
    [[nodiscard]] bool step() noexcept;

    /** @brief Returns the current progress snapshot. @ingroup hbrick_bench */
    [[nodiscard]] ReachabilityBenchmarkProgress progress() const noexcept;

    /** @brief Returns the report; valid when progress stage is Finished. @ingroup hbrick_bench */
    [[nodiscard]] const ReachabilityBenchmarkReport& report() const noexcept;

    /**
     * @brief Runs a benchmark to completion on the calling thread.
     * @ingroup hbrick_bench
     */
    [[nodiscard]] static ReachabilityBenchmarkReport run(
        const CsrGraph& graph,
        std::span<const uint32_t> query_universe,
        ReachabilityBenchmarkConfig config
    );

    /** @brief Grid overload for benchmarks that include BRICK baselines. @ingroup hbrick_bench */
    [[nodiscard]] static ReachabilityBenchmarkReport run(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        std::span<const uint32_t> query_universe,
        ReachabilityBenchmarkConfig config
    );

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace hbrick
