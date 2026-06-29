#include "hbrick/bench/reachability_benchmark.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "hbrick/bench/bench_timer.hpp"
#include "hbrick/bench/reachability_benchmark_format.hpp"
#include "hbrick/bench/reachability_benchmark_util.hpp"
#include "hbrick/baselines/closure_matrix_builder.hpp"
#include "hbrick/baselines/csr_bfs_baseline.hpp"
#include "hbrick/baselines/csr_dfs_baseline.hpp"
#include "hbrick/baselines/full_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_search_baseline.hpp"
#include "hbrick/baselines/two_hop_baseline.hpp"
#include "hbrick/baselines/grail_baseline.hpp"
#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/bit/kleene_squaring_options.hpp"
#include "hbrick/baselines/brick_closure_baseline.hpp"
#include "hbrick/baselines/brick_search_baseline.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/bench/process_memory.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace hbrick {

namespace {

[[nodiscard]] uint64_t monotonicNowNanoseconds() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

[[nodiscard]] std::vector<ReachabilityBaselineId> defaultMethodList() {
    return {
        ReachabilityBaselineId::CsrBfs,
        ReachabilityBaselineId::CsrDfs,
        ReachabilityBaselineId::SccDagSearch,
        ReachabilityBaselineId::SccDagClosure,
        ReachabilityBaselineId::TwoHop,
        ReachabilityBaselineId::Grail,
        ReachabilityBaselineId::BrickSearch,
        ReachabilityBaselineId::BrickClosure,
        ReachabilityBaselineId::FullClosure,
    };
}

void orderMethodsForBenchmark(std::vector<ReachabilityBaselineId>& methods) {
    const auto full_closure_it = std::find(
        methods.begin(),
        methods.end(),
        ReachabilityBaselineId::FullClosure
    );
    if (full_closure_it != methods.end()
        && full_closure_it + 1 != methods.end()) {
        const ReachabilityBaselineId full_closure = *full_closure_it;
        methods.erase(full_closure_it);
        methods.push_back(full_closure);
    }
}

void generateQueryPairs(
    const std::span<const uint32_t> query_universe,
    const uint32_t query_count,
    const uint64_t pair_seed,
    std::vector<ReachabilityQueryPair>& pairs
) {
    pairs.clear();
    if (query_universe.empty() || query_count == 0U) {
        return;
    }

    pairs.resize(query_count);
    std::mt19937_64 rng(pair_seed);
    std::uniform_int_distribution<std::size_t> distribution(
        0U,
        query_universe.size() - 1U
    );

    for (ReachabilityQueryPair& pair : pairs) {
        pair.source = query_universe[distribution(rng)];
        pair.target = query_universe[distribution(rng)];
    }
}

void finalizeQueryStatistics(
    std::vector<uint64_t>& query_times_ns,
    QueryTimingStatistics& stats
) {
    stats.count = static_cast<uint64_t>(query_times_ns.size());
    if (query_times_ns.empty()) {
        return;
    }

    stats.total_nanoseconds = 0U;
    stats.min_nanoseconds = std::numeric_limits<uint64_t>::max();
    stats.max_nanoseconds = 0U;

    for (const uint64_t elapsed : query_times_ns) {
        stats.total_nanoseconds += elapsed;
        stats.min_nanoseconds = std::min(stats.min_nanoseconds, elapsed);
        stats.max_nanoseconds = std::max(stats.max_nanoseconds, elapsed);
    }

    stats.mean_nanoseconds =
        static_cast<double>(stats.total_nanoseconds)
        / static_cast<double>(stats.count);

    std::sort(query_times_ns.begin(), query_times_ns.end());
    stats.median_nanoseconds = query_times_ns[query_times_ns.size() / 2U];
    const std::size_t p95_index =
        std::min(
            query_times_ns.size() - 1U,
            static_cast<std::size_t>(
                static_cast<double>(query_times_ns.size()) * 0.95
            )
        );
    stats.p95_nanoseconds = query_times_ns[p95_index];

    if (stats.total_nanoseconds > 0U) {
        stats.queries_per_second =
            static_cast<double>(stats.count)
            / (static_cast<double>(stats.total_nanoseconds) / 1.0e9);
    }
}

void refreshPartialQueryStatistics(
    const std::vector<uint64_t>& query_times_ns,
    QueryTimingStatistics& stats
) noexcept {
    stats.count = static_cast<uint64_t>(query_times_ns.size());
    if (query_times_ns.empty()) {
        stats.total_nanoseconds = 0U;
        stats.mean_nanoseconds = 0.0;
        stats.queries_per_second = 0.0;
        return;
    }

    uint64_t total_nanoseconds = 0U;
    for (const uint64_t elapsed : query_times_ns) {
        total_nanoseconds += elapsed;
    }

    stats.total_nanoseconds = total_nanoseconds;
    stats.mean_nanoseconds =
        static_cast<double>(total_nanoseconds)
        / static_cast<double>(stats.count);
    if (total_nanoseconds > 0U) {
        stats.queries_per_second =
            static_cast<double>(stats.count)
            / (static_cast<double>(total_nanoseconds) / 1.0e9);
    } else {
        stats.queries_per_second = 0.0;
    }
}

[[nodiscard]] uint64_t estimateCsrGraphBytes(const CsrGraph& graph) noexcept {
    const uint64_t vertices = graph.numVertices();
    const uint64_t edges = graph.numEdges();
    return (vertices + 1ULL) * sizeof(uint32_t) + edges * sizeof(uint32_t);
}

struct BaselineInstances {
    CsrBfsBaseline csr_bfs;
    CsrDfsBaseline csr_dfs;
    SccDagSearchBaseline scc_search;
    SccDagClosureBaseline scc_closure;
    FullClosureBaseline full_closure;
    TwoHopBaseline two_hop;
    GrailBaseline grail;
    BrickSearchBaseline brick_search;
    BrickClosureBaseline brick_closure;
    HBrickBaseline hbrick;
};

[[nodiscard]] HBrickConfig hbrickConfigFromBenchmark(
    const ReachabilityBenchmarkConfig& config
) noexcept {
    HBrickConfig hbrick_config{};
    hbrick_config.base_tile_size = config.brick_tile_size;
    hbrick_config.group_size = config.hbrick_group_size;
    hbrick_config.max_depth = config.hbrick_max_depth;
    hbrick_config.max_memory_bytes = config.max_memory_bytes;
    return hbrick_config;
}

[[nodiscard]] bool gridBaselineRequiresMazeContext(
    const ReachabilityBaselineId method
) noexcept {
    return method == ReachabilityBaselineId::BrickSearch
        || method == ReachabilityBaselineId::BrickClosure
        || method == ReachabilityBaselineId::HBrick;
}

struct GridBenchmarkContext {
    bool has_grid = false;
    const DirectedGridGraph* grid_graph = nullptr;
    const MazeLayout* layout = nullptr;
};

[[nodiscard]] BaselineStatus preprocessBaseline(
    const ReachabilityBaselineId method,
    BaselineInstances& baselines,
    const CsrGraph& graph,
    GraphSearchScratch& preprocess_scratch,
    const ReachabilityBenchmarkConfig& config,
    const GridBenchmarkContext& grid_context
) {
    switch (method) {
        case ReachabilityBaselineId::CsrBfs:
            baselines.csr_bfs.preprocess(graph);
            return baselines.csr_bfs.status();
        case ReachabilityBaselineId::CsrDfs:
            baselines.csr_dfs.preprocess(graph);
            return baselines.csr_dfs.status();
        case ReachabilityBaselineId::SccDagSearch:
            baselines.scc_search.preprocess(graph, preprocess_scratch);
            return baselines.scc_search.status();
        case ReachabilityBaselineId::SccDagClosure:
        case ReachabilityBaselineId::FullClosure:
            return BaselineStatus::Failed;
        case ReachabilityBaselineId::BrickSearch:
            if (!grid_context.has_grid || grid_context.grid_graph == nullptr
                || grid_context.layout == nullptr) {
                return BaselineStatus::SkippedByPolicy;
            }
            baselines.brick_search.preprocess(
                *grid_context.grid_graph,
                *grid_context.layout,
                config.brick_tile_size,
                config.max_memory_bytes
            );
            return baselines.brick_search.status();
        case ReachabilityBaselineId::BrickClosure: {
            if (!grid_context.has_grid || grid_context.grid_graph == nullptr
                || grid_context.layout == nullptr) {
                return BaselineStatus::SkippedByPolicy;
            }
            KleeneSquaringOptions kleene_options{};
            kleene_options.use_parallel = config.kleene_use_parallel;
            kleene_options.num_threads = config.kleene_num_threads;
            baselines.brick_closure.preprocess(
                *grid_context.grid_graph,
                *grid_context.layout,
                config.brick_tile_size,
                config.max_memory_bytes,
                kleene_options
            );
            return baselines.brick_closure.status();
        }
        case ReachabilityBaselineId::HBrick:
            if (!grid_context.has_grid || grid_context.grid_graph == nullptr
                || grid_context.layout == nullptr) {
                return BaselineStatus::SkippedByPolicy;
            }
            baselines.hbrick.preprocess(
                *grid_context.grid_graph,
                *grid_context.layout,
                hbrickConfigFromBenchmark(config)
            );
            return baselines.hbrick.status();
        case ReachabilityBaselineId::TwoHop:
            baselines.two_hop.preprocess(
                graph,
                preprocess_scratch,
                config.max_memory_bytes
            );
            return baselines.two_hop.status();
        case ReachabilityBaselineId::Grail:
            baselines.grail.preprocess(
                graph,
                config.grail_params,
                config.max_memory_bytes
            );
            return baselines.grail.status();
    }

    return BaselineStatus::Failed;
}

[[nodiscard]] uint64_t indexStorageBytesForBaseline(
    const ReachabilityBaselineId method,
    const BaselineInstances& baselines,
    const CsrGraph& graph
) noexcept {
    if (!baselineBuildsPreprocessIndex(method)) {
        return 0U;
    }

    switch (method) {
        case ReachabilityBaselineId::CsrBfs:
        case ReachabilityBaselineId::CsrDfs:
            return 0U;
        case ReachabilityBaselineId::SccDagSearch:
            return estimateCsrGraphBytes(graph);
        case ReachabilityBaselineId::SccDagClosure:
            return baselines.scc_closure.indexStorageBytes();
        case ReachabilityBaselineId::FullClosure:
            return baselines.full_closure.indexStorageBytes();
        case ReachabilityBaselineId::TwoHop:
            return baselines.two_hop.labelStorageBytes();
        case ReachabilityBaselineId::Grail:
            return baselines.grail.labelStorageBytes();
        case ReachabilityBaselineId::BrickSearch:
            return baselines.brick_search.indexStorageBytes();
        case ReachabilityBaselineId::BrickClosure:
            return baselines.brick_closure.indexStorageBytes();
        case ReachabilityBaselineId::HBrick:
            return baselines.hbrick.indexStorageBytes();
    }

    return 0U;
}

struct QueryExecutionStats {
    ReachabilityAnswer answer = ReachabilityAnswer::Unreachable;
    uint64_t elapsed_nanoseconds = 0U;
    bool grail_tree_certified = false;
};

[[nodiscard]] QueryExecutionStats executeTimedQuery(
    const ReachabilityBaselineId method,
    BaselineInstances& baselines,
    const ReachabilityQueryPair& pair,
    GraphSearchScratch& scratch
) {
    QueryExecutionStats stats;
    const uint64_t start = monotonicNowNanoseconds();

    switch (method) {
        case ReachabilityBaselineId::CsrBfs:
            stats.answer = baselines.csr_bfs.query(pair.source, pair.target, scratch);
            break;
        case ReachabilityBaselineId::CsrDfs:
            stats.answer = baselines.csr_dfs.query(pair.source, pair.target, scratch);
            break;
        case ReachabilityBaselineId::SccDagSearch:
            stats.answer = baselines.scc_search.query(pair.source, pair.target, scratch);
            break;
        case ReachabilityBaselineId::SccDagClosure:
            stats.answer = baselines.scc_closure.query(pair.source, pair.target);
            break;
        case ReachabilityBaselineId::FullClosure:
            stats.answer = baselines.full_closure.query(pair.source, pair.target);
            break;
        case ReachabilityBaselineId::TwoHop:
            stats.answer = baselines.two_hop.query(pair.source, pair.target);
            break;
        case ReachabilityBaselineId::Grail: {
            const GrailQueryOutcome outcome = baselines.grail.queryDetailed(
                pair.source,
                pair.target,
                scratch
            );
            stats.answer = outcome.answer;
            stats.grail_tree_certified = outcome.tree_certified;
            break;
        }
        case ReachabilityBaselineId::BrickSearch:
            stats.answer = baselines.brick_search.query(pair.source, pair.target);
            break;
        case ReachabilityBaselineId::BrickClosure:
            stats.answer = baselines.brick_closure.query(pair.source, pair.target);
            break;
        case ReachabilityBaselineId::HBrick:
            stats.answer = baselines.hbrick.query(pair.source, pair.target);
            break;
    }

    stats.elapsed_nanoseconds = monotonicNowNanoseconds() - start;
    return stats;
}

void executeWarmupQuery(
    const ReachabilityBaselineId method,
    BaselineInstances& baselines,
    const ReachabilityQueryPair& pair,
    GraphSearchScratch& scratch
) {
    (void)executeTimedQuery(method, baselines, pair, scratch);
}

[[nodiscard]] ReachabilityAnswer referenceBfsAnswer(
    const CsrGraph& graph,
    const ReachabilityQueryPair& pair,
    GraphSearchScratch& scratch
) {
    return Bfs::reachable(graph, pair.source, pair.target, scratch);
}

void applySpeedups(ReachabilityBenchmarkReport& report) noexcept {
    for (BaselineBenchmarkMetrics& metrics : report.methods) {
        metrics.total_benchmark_nanoseconds =
            metrics.preprocess_nanoseconds
            + metrics.warmup_nanoseconds
            + metrics.query_stats.total_nanoseconds;
    }

    double reference_mean = 0.0;
    uint64_t reference_total = 0U;
    for (const BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.method == ReachabilityBaselineId::CsrBfs
            && metrics.status == BaselineStatus::Completed
            && metrics.query_stats.count > 0U) {
            reference_mean = metrics.query_stats.mean_nanoseconds;
            reference_total = metrics.total_benchmark_nanoseconds;
            break;
        }
    }

    report.reference_bfs_mean_query_nanoseconds = reference_mean;
    report.reference_bfs_total_benchmark_nanoseconds = reference_total;
    if (reference_mean <= 0.0 && reference_total == 0U) {
        return;
    }

    for (BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.status != BaselineStatus::Completed) {
            metrics.speedup_vs_bfs = 0.0;
            metrics.total_speedup_vs_bfs = 0.0;
            continue;
        }

        if (metrics.query_stats.count > 0U
            && metrics.query_stats.mean_nanoseconds > 0.0
            && reference_mean > 0.0) {
            metrics.speedup_vs_bfs =
                reference_mean / metrics.query_stats.mean_nanoseconds;
        } else {
            metrics.speedup_vs_bfs = 0.0;
        }

        if (metrics.total_benchmark_nanoseconds > 0U && reference_total > 0U) {
            metrics.total_speedup_vs_bfs =
                static_cast<double>(reference_total)
                / static_cast<double>(metrics.total_benchmark_nanoseconds);
        } else {
            metrics.total_speedup_vs_bfs = 0.0;
        }
    }
}

enum class JobPhase : uint8_t {
    GeneratePairs,
    RunMethod,
    Done,
};

enum class MethodPhase : uint8_t {
    Preprocess,
    Warmup,
    Query,
    Correctness,
};

[[nodiscard]] bool usesIncrementalBrickPreprocess(
    const ReachabilityBaselineId method
) noexcept {
    return method == ReachabilityBaselineId::BrickClosure
        || method == ReachabilityBaselineId::BrickSearch;
}

[[nodiscard]] bool usesIncrementalClosurePreprocess(
    const ReachabilityBaselineId method
) noexcept {
    return method == ReachabilityBaselineId::FullClosure
        || method == ReachabilityBaselineId::SccDagClosure;
}

void formatDurationSeconds(
    char* buffer,
    const std::size_t size,
    const double nanoseconds
) noexcept {
    const double seconds = nanoseconds / 1.0e9;
    if (seconds >= 3600.0) {
        std::snprintf(buffer, size, "%.1f h", seconds / 3600.0);
        return;
    }

    if (seconds >= 60.0) {
        std::snprintf(buffer, size, "%.1f min", seconds / 60.0);
        return;
    }

    if (seconds >= 1.0) {
        std::snprintf(buffer, size, "%.2f s", seconds);
        return;
    }

    if (seconds >= 0.001) {
        std::snprintf(buffer, size, "%.2f ms", seconds * 1000.0);
        return;
    }

    std::snprintf(buffer, size, "%.0f ns", nanoseconds);
}

void setMemoryCapSkipDetail(
    BaselineBenchmarkMetrics& metrics,
    const ReachabilityBaselineId method,
    const uint64_t estimated_bytes,
    const uint64_t max_memory_bytes
) {
    char estimated_label[32];
    char cap_label[32];
    formatBenchmarkBytes(estimated_label, sizeof(estimated_label), estimated_bytes);
    formatBenchmarkBytes(cap_label, sizeof(cap_label), max_memory_bytes);
    char buffer[256];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%s index size (%s) exceeds the benchmark memory cap (%s).",
        reachabilityBaselineName(method),
        estimated_label,
        cap_label
    );
    metrics.policy_skip_detail = buffer;
}

[[nodiscard]] uint64_t estimatedIndexBytesForMethod(
    const ReachabilityBaselineId method,
    const CsrGraph& graph,
    const ReachabilityBenchmarkConfig& config
) noexcept {
    switch (method) {
        case ReachabilityBaselineId::FullClosure:
        case ReachabilityBaselineId::SccDagClosure:
            return ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes(
                graph.numVertices()
            );
        case ReachabilityBaselineId::TwoHop:
            return 0U;
        case ReachabilityBaselineId::Grail:
            return GrailBaseline::estimateLabelBytes(
                graph.numVertices(),
                config.grail_params.num_trees
            );
        case ReachabilityBaselineId::CsrBfs:
        case ReachabilityBaselineId::CsrDfs:
        case ReachabilityBaselineId::SccDagSearch:
        case ReachabilityBaselineId::BrickSearch:
        case ReachabilityBaselineId::BrickClosure:
        case ReachabilityBaselineId::HBrick:
            return 0U;
    }

    return 0U;
}

[[nodiscard]] uint64_t estimatedClosureQueryNanoseconds(
    const ReachabilityBenchmarkReport& report,
    const ReachabilityBenchmarkConfig& config
) noexcept {
    for (const BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.method == ReachabilityBaselineId::CsrBfs
            && metrics.query_stats.count > 0U
            && metrics.query_stats.mean_nanoseconds > 0.0) {
            const double per_query = std::max(
                100.0,
                metrics.query_stats.mean_nanoseconds / 10.0
            );
            return static_cast<uint64_t>(
                per_query * static_cast<double>(config.query_count)
            );
        }
    }

    return static_cast<uint64_t>(config.query_count) * 100ULL;
}

void setClosureProjectedSpeedupSkipDetail(
    BaselineBenchmarkMetrics& metrics,
    const ReachabilityBaselineId method,
    const double projected_speedup,
    const uint32_t pivots_completed,
    const uint32_t pivot_total,
    const uint64_t bfs_total_nanoseconds,
    const uint64_t projected_total_nanoseconds,
    const double minimum_projected_speedup
) {
    char speedup_label[32];
    char bfs_total_label[32];
    char projected_total_label[32];
    formatBenchmarkSpeedupRatio(speedup_label, sizeof(speedup_label), projected_speedup);
    formatDurationSeconds(
        bfs_total_label,
        sizeof(bfs_total_label),
        static_cast<double>(bfs_total_nanoseconds)
    );
    formatDurationSeconds(
        projected_total_label,
        sizeof(projected_total_label),
        static_cast<double>(projected_total_nanoseconds)
    );

    char buffer[512];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "Projected total speedup vs CsrBfs is %s (minimum %.2fx); stopped %s "
        "preprocessing after %u / %u Warshall pivots "
        "(CsrBfs end-to-end %s vs projected %s end-to-end).",
        speedup_label,
        minimum_projected_speedup,
        reachabilityBaselineName(method),
        pivots_completed,
        pivot_total,
        bfs_total_label,
        projected_total_label
    );
    metrics.policy_skip_detail = buffer;
    metrics.projected_total_speedup_vs_bfs = projected_speedup;
}

[[nodiscard]] uint32_t closureAbortMinPivots(const uint32_t pivot_total) noexcept {
    if (pivot_total <= 1U) {
        return 1U;
    }

    return std::max(4U, pivot_total / 16U);
}

[[nodiscard]] uint64_t referenceBfsTotalBenchmark(
    const ReachabilityBenchmarkReport& report
) noexcept {
    for (const BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.method == ReachabilityBaselineId::CsrBfs
            && metrics.total_benchmark_nanoseconds > 0U) {
            return metrics.total_benchmark_nanoseconds;
        }
    }

    return report.reference_bfs_total_benchmark_nanoseconds;
}

[[nodiscard]] uint64_t referenceBfsWarmupNanoseconds(
    const ReachabilityBenchmarkReport& report
) noexcept {
    for (const BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.method == ReachabilityBaselineId::CsrBfs) {
            return metrics.warmup_nanoseconds;
        }
    }

    return 0U;
}

struct ClosureSpeedupProjection {
    double speedup_vs_bfs = 0.0;
    uint64_t projected_total_nanoseconds = 0U;
};

[[nodiscard]] ClosureSpeedupProjection projectedClosureTotalSpeedupVsBfs(
    const ReachabilityBenchmarkReport& report,
    const ReachabilityBenchmarkConfig& config,
    const uint64_t setup_nanoseconds,
    const uint64_t pivot_elapsed_nanoseconds,
    const uint32_t pivots_completed,
    const uint32_t pivot_total
) noexcept {
    ClosureSpeedupProjection projection;
    const uint64_t bfs_total_nanoseconds = referenceBfsTotalBenchmark(report);
    if (bfs_total_nanoseconds == 0U || pivots_completed == 0U || pivot_total == 0U) {
        return projection;
    }

    const double projected_pivot_nanoseconds =
        static_cast<double>(pivot_elapsed_nanoseconds)
        * static_cast<double>(pivot_total)
        / static_cast<double>(pivots_completed);
    const double projected_preprocess_nanoseconds =
        static_cast<double>(setup_nanoseconds) + projected_pivot_nanoseconds;
    const double projected_total_nanoseconds =
        projected_preprocess_nanoseconds
        + static_cast<double>(referenceBfsWarmupNanoseconds(report))
        + static_cast<double>(estimatedClosureQueryNanoseconds(report, config));
    if (projected_total_nanoseconds <= 0.0) {
        return projection;
    }

    projection.projected_total_nanoseconds =
        static_cast<uint64_t>(projected_total_nanoseconds);
    projection.speedup_vs_bfs =
        static_cast<double>(bfs_total_nanoseconds) / projected_total_nanoseconds;
    return projection;
}

[[nodiscard]] uint32_t closurePivotBatchSize(const uint32_t pivot_total) noexcept {
    if (pivot_total <= 1U) {
        return 1U;
    }

    return std::max(1U, pivot_total / 32U);
}

[[nodiscard]] uint64_t workUnitsForMethod(
    const ReachabilityBaselineId method,
    const uint32_t num_vertices,
    const ReachabilityBenchmarkConfig& config,
    const uint32_t pair_count
) noexcept {
    const uint32_t checks = std::min(config.correctness_check_count, pair_count);
    uint64_t preprocess_units = 1ULL;
    if (usesIncrementalClosurePreprocess(method) && num_vertices > 0U) {
        preprocess_units = num_vertices;
    } else if (usesIncrementalBrickPreprocess(method) && num_vertices > 0U) {
        preprocess_units = static_cast<uint64_t>(num_vertices) + 64ULL;
    } else if (method == ReachabilityBaselineId::SccDagSearch && num_vertices > 0U) {
        preprocess_units = static_cast<uint64_t>(num_vertices) * 3ULL + 2ULL;
    }

    return preprocess_units
        + static_cast<uint64_t>(config.warmup_queries)
        + static_cast<uint64_t>(config.query_count)
        + static_cast<uint64_t>(checks);
}

[[nodiscard]] uint64_t remainingWorkUnitsForMethod(
    const ReachabilityBaselineId method,
    const MethodPhase method_phase,
    const ReachabilityBenchmarkConfig& config,
    const uint32_t pair_count,
    const uint32_t closure_pivots_completed,
    const uint32_t closure_pivot_total,
    const uint32_t query_index,
    const uint32_t correctness_index
) noexcept {
    const uint32_t checks = std::min(config.correctness_check_count, pair_count);
    uint64_t remaining = 0ULL;

        if (method_phase == MethodPhase::Preprocess) {
        if (usesIncrementalClosurePreprocess(method) && closure_pivot_total > 0U) {
            remaining += static_cast<uint64_t>(closure_pivot_total)
                - static_cast<uint64_t>(closure_pivots_completed);
        } else if (usesIncrementalBrickPreprocess(method) && closure_pivot_total > 0U) {
            remaining += static_cast<uint64_t>(closure_pivot_total)
                - static_cast<uint64_t>(closure_pivots_completed);
        } else if (method == ReachabilityBaselineId::SccDagSearch
            && closure_pivot_total > 0U) {
            remaining += static_cast<uint64_t>(closure_pivot_total)
                - static_cast<uint64_t>(closure_pivots_completed);
        } else {
            remaining += 1ULL;
        }
    }

    if (method_phase == MethodPhase::Preprocess
        || method_phase == MethodPhase::Warmup) {
        const uint32_t warmup_remaining =
            config.warmup_queries
            - (method_phase == MethodPhase::Warmup ? query_index : 0U);
        remaining += static_cast<uint64_t>(warmup_remaining);
    }

    if (method_phase == MethodPhase::Preprocess
        || method_phase == MethodPhase::Warmup
        || method_phase == MethodPhase::Query) {
        const uint32_t query_remaining =
            config.query_count
            - (method_phase == MethodPhase::Query ? query_index : 0U);
        remaining += static_cast<uint64_t>(query_remaining);
    }

    const uint32_t checks_remaining =
        checks - (method_phase == MethodPhase::Correctness ? correctness_index : 0U);
    remaining += static_cast<uint64_t>(checks_remaining);
    return remaining;
}

}  // namespace

const char* reachabilityBaselineName(const ReachabilityBaselineId id) noexcept {
    switch (id) {
        case ReachabilityBaselineId::CsrBfs:
            return "CsrBfs";
        case ReachabilityBaselineId::CsrDfs:
            return "CsrDfs";
        case ReachabilityBaselineId::SccDagSearch:
            return "SccDagSearch";
        case ReachabilityBaselineId::SccDagClosure:
            return "SccDagClosure";
        case ReachabilityBaselineId::FullClosure:
            return "FullClosure";
        case ReachabilityBaselineId::TwoHop:
            return "TwoHop";
        case ReachabilityBaselineId::Grail:
            return "Grail";
        case ReachabilityBaselineId::BrickSearch:
            return "BrickSearch";
        case ReachabilityBaselineId::BrickClosure:
            return "BrickClosure";
        case ReachabilityBaselineId::HBrick:
            return "HBrick";
    }

    return "Unknown";
}

ReachabilityBenchmarkConfig ReachabilityBenchmarkConfig::allMethods() noexcept {
    ReachabilityBenchmarkConfig config;
    config.methods = defaultMethodList();
    return config;
}

struct ReachabilityBenchmarkJob::Impl {
    CsrGraph graph;
    DirectedGridGraph grid_graph;
    MazeLayout layout{1U, 1U, true};
    bool has_grid_benchmark = false;
    ReachabilityBenchmarkConfig config;
    std::vector<uint32_t> query_universe;
    std::vector<ReachabilityQueryPair> pairs;
    std::vector<ReachabilityBaselineId> methods;
    BaselineInstances baselines;
    GraphSearchScratch preprocess_scratch{0U};
    GraphSearchScratch query_scratch{0U};

    ReachabilityBenchmarkProgress progress{};
    ReachabilityBenchmarkReport report{};
    JobPhase phase = JobPhase::Done;
    MethodPhase method_phase = MethodPhase::Preprocess;
    std::size_t method_index = 0U;
    std::size_t query_index = 0U;
    std::size_t correctness_index = 0U;
    std::vector<uint64_t> current_query_times;
    double current_positive_ns_sum = 0.0;
    double current_negative_ns_sum = 0.0;
    bool active_job = false;
    bool incremental_closure_running = false;
    uint64_t incremental_closure_setup_nanoseconds = 0U;
    uint64_t incremental_closure_pivot_start_ns = 0U;
    uint64_t incremental_closure_rss_before = 0U;
    bool incremental_scc_search_running = false;
    uint64_t incremental_scc_search_started_ns = 0U;
    bool incremental_brick_running = false;
    uint64_t incremental_brick_started_ns = 0U;
    uint64_t incremental_brick_rss_before = 0U;
    std::atomic<bool> skip_current_method_requested{false};

    [[nodiscard]] GridBenchmarkContext gridContext() const noexcept {
        GridBenchmarkContext context;
        if (has_grid_benchmark) {
            context.has_grid = true;
            context.grid_graph = &grid_graph;
            context.layout = &layout;
        }
        return context;
    }

    template<typename Baseline>
    void skipClosurePreprocessByUser(
        BaselineBenchmarkMetrics& metrics,
        const ReachabilityBaselineId method,
        Baseline& baseline
    ) {
        baseline.abortPreprocessSkippedByPolicy();
        metrics.status = BaselineStatus::SkippedByPolicy;
        metrics.estimated_index_bytes = 0U;
        metrics.preprocess_rss_delta_bytes = 0U;
        progress.preprocess_started_ns = 0U;
        incremental_closure_running = false;
        metrics.policy_skip_detail =
            std::string("Stopped by user during ")
            + reachabilityBaselineName(method)
            + " preprocessing after "
            + std::to_string(metrics.warshall_pivots_completed)
            + " / "
            + std::to_string(metrics.warshall_pivot_total)
            + " Warshall pivots.";
        refreshTotalBenchmarkNanoseconds(metrics);
        skipRemainingWorkForCurrentMethod();
    }

    void advanceWork(const uint64_t units) noexcept {
        if (units == 0U) {
            return;
        }

        progress.work_completed += units;
        if (progress.work_total > 0U && progress.work_completed > progress.work_total) {
            progress.work_completed = progress.work_total;
        }
    }

    void setStageWork(const uint32_t completed, const uint32_t total) noexcept {
        progress.stage_work_completed = completed;
        progress.stage_work_total = total;
    }

    void refreshTotalBenchmarkNanoseconds(BaselineBenchmarkMetrics& metrics) const noexcept {
        metrics.total_benchmark_nanoseconds =
            metrics.preprocess_nanoseconds
            + metrics.warmup_nanoseconds
            + metrics.query_stats.total_nanoseconds;
    }

    void advanceToNextMethod() noexcept {
        ++method_index;
        query_index = 0U;
        correctness_index = 0U;
        method_phase = MethodPhase::Preprocess;
        incremental_closure_running = false;
        incremental_closure_setup_nanoseconds = 0U;
        incremental_closure_pivot_start_ns = 0U;
        incremental_closure_rss_before = 0U;
        if (incremental_scc_search_running) {
            baselines.scc_search.cancelPreprocess();
            incremental_scc_search_running = false;
        }
        if (incremental_brick_running) {
            baselines.brick_closure.cancelPreprocess();
            baselines.brick_search.cancelPreprocess();
            incremental_brick_running = false;
        }
        incremental_scc_search_started_ns = 0U;
        incremental_brick_started_ns = 0U;
        progress.methods_completed = static_cast<uint32_t>(method_index);
        progress.preprocess_started_ns = 0U;
        current_query_times.clear();
    }

    void skipRemainingWorkForCurrentMethod() noexcept {
        if (method_index >= methods.size()) {
            return;
        }

        const ReachabilityBaselineId method = methods[method_index];
        uint32_t closure_pivots_completed = 0U;
        uint32_t closure_pivot_total = graph.numVertices();
        if (method == ReachabilityBaselineId::FullClosure) {
            closure_pivots_completed =
                baselines.full_closure.preprocessPivotsCompleted();
            closure_pivot_total = std::max(
                closure_pivot_total,
                baselines.full_closure.preprocessPivotTotal()
            );
        } else if (method == ReachabilityBaselineId::SccDagClosure) {
            closure_pivots_completed =
                baselines.scc_closure.preprocessPivotsCompleted();
            closure_pivot_total = std::max(
                closure_pivot_total,
                baselines.scc_closure.preprocessPivotTotal()
            );
        } else if (usesIncrementalBrickPreprocess(method) && incremental_brick_running) {
            const uint64_t work_completed =
                method == ReachabilityBaselineId::BrickClosure
                    ? baselines.brick_closure.preprocessWorkCompleted()
                    : baselines.brick_search.preprocessWorkCompleted();
            const uint64_t work_total =
                method == ReachabilityBaselineId::BrickClosure
                    ? baselines.brick_closure.preprocessWorkTotal()
                    : baselines.brick_search.preprocessWorkTotal();
            closure_pivots_completed = static_cast<uint32_t>(std::min<uint64_t>(
                work_completed,
                std::numeric_limits<uint32_t>::max()
            ));
            closure_pivot_total = static_cast<uint32_t>(std::min<uint64_t>(
                work_total,
                std::numeric_limits<uint32_t>::max()
            ));
        } else if (method == ReachabilityBaselineId::SccDagSearch
            && incremental_scc_search_running) {
            closure_pivots_completed = static_cast<uint32_t>(std::min<uint64_t>(
                baselines.scc_search.preprocessWorkCompleted(),
                std::numeric_limits<uint32_t>::max()
            ));
            closure_pivot_total = static_cast<uint32_t>(std::min<uint64_t>(
                baselines.scc_search.preprocessWorkTotal(),
                std::numeric_limits<uint32_t>::max()
            ));
        }
        advanceWork(remainingWorkUnitsForMethod(
            method,
            method_phase,
            config,
            static_cast<uint32_t>(pairs.size()),
            closure_pivots_completed,
            closure_pivot_total,
            static_cast<uint32_t>(query_index),
            static_cast<uint32_t>(correctness_index)
        ));
        advanceToNextMethod();
    }

    enum class IncrementalClosurePreprocessResult : uint8_t {
        StepIncomplete,
        Skipped,
        CompletedWarmup,
    };

    template<typename Baseline>
    IncrementalClosurePreprocessResult stepIncrementalClosurePreprocess(
        Baseline& baseline,
        BaselineBenchmarkMetrics& metrics,
        const ReachabilityBaselineId method
    ) {
        if (skip_current_method_requested.exchange(false, std::memory_order_acq_rel)) {
            skipClosurePreprocessByUser(metrics, method, baseline);
            return IncrementalClosurePreprocessResult::Skipped;
        }

        if (!incremental_closure_running) {
            incremental_closure_rss_before = currentProcessRssBytes();
            const uint64_t setup_start = BenchTimer::steadyNowNanoseconds();
            if constexpr (std::is_same_v<Baseline, SccDagClosureBaseline>) {
                baseline.beginPreprocess(graph, preprocess_scratch, config.max_memory_bytes);
            } else {
                baseline.beginPreprocess(graph, config.max_memory_bytes);
            }
            incremental_closure_setup_nanoseconds =
                BenchTimer::steadyNowNanoseconds() - setup_start;
            metrics.status = baseline.status();
            if (!baseline.preprocessActive()) {
                metrics.preprocess_nanoseconds = 0U;
                metrics.estimated_index_bytes = 0U;
                metrics.preprocess_rss_delta_bytes = 0U;
                if (metrics.status == BaselineStatus::SkippedByPolicy) {
                    setMemoryCapSkipDetail(
                        metrics,
                        method,
                        estimatedIndexBytesForMethod(method, graph, config),
                        config.max_memory_bytes
                    );
                }
                refreshTotalBenchmarkNanoseconds(metrics);
                skipRemainingWorkForCurrentMethod();
                return IncrementalClosurePreprocessResult::Skipped;
            }

            incremental_closure_running = true;
            incremental_closure_pivot_start_ns = BenchTimer::steadyNowNanoseconds();
            progress.preprocess_started_ns = incremental_closure_pivot_start_ns;
            metrics.warshall_matrix_order = baseline.preprocessPivotTotal();
            metrics.warshall_pivot_total = baseline.preprocessPivotTotal();
            metrics.warshall_pivots_completed = 0U;
            setStageWork(0U, baseline.preprocessPivotTotal());
        }

        const uint32_t pivot_total = baseline.preprocessPivotTotal();
        const uint32_t pivots_before = baseline.preprocessPivotsCompleted();
        const uint32_t pivot_batch = closurePivotBatchSize(pivot_total);
        const bool finished = baseline.stepPreprocessPivots(pivot_batch);
        const uint32_t pivots_after = baseline.preprocessPivotsCompleted();
        metrics.warshall_matrix_order = pivot_total;
        metrics.warshall_pivot_total = pivot_total;
        metrics.warshall_pivots_completed = pivots_after;
        advanceWork(static_cast<uint64_t>(pivots_after - pivots_before));
        setStageWork(pivots_after, pivot_total);

        const uint64_t pivot_elapsed =
            BenchTimer::steadyNowNanoseconds() - incremental_closure_pivot_start_ns;
        const uint64_t live_preprocess =
            incremental_closure_setup_nanoseconds + pivot_elapsed;
        metrics.preprocess_nanoseconds = live_preprocess;
        refreshTotalBenchmarkNanoseconds(metrics);

        if (!finished && config.closure_enable_projected_speedup_early_stop
            && pivots_after >= closureAbortMinPivots(pivot_total)) {
            const uint64_t bfs_total = referenceBfsTotalBenchmark(report);
            if (bfs_total > 0U) {
                const ClosureSpeedupProjection projection =
                    projectedClosureTotalSpeedupVsBfs(
                        report,
                        config,
                        incremental_closure_setup_nanoseconds,
                        pivot_elapsed,
                        pivots_after,
                        pivot_total
                    );
                metrics.projected_total_speedup_vs_bfs = projection.speedup_vs_bfs;
                const double minimum_speedup = static_cast<double>(
                    config.closure_min_projected_total_speedup_vs_bfs
                );
                if (projection.speedup_vs_bfs > 0.0
                    && projection.speedup_vs_bfs < minimum_speedup) {
                    baseline.abortPreprocessSkippedByPolicy();
                    metrics.status = BaselineStatus::SkippedByPolicy;
                    metrics.estimated_index_bytes = 0U;
                    metrics.preprocess_rss_delta_bytes = 0U;
                    progress.preprocess_started_ns = 0U;
                    incremental_closure_running = false;
                    setClosureProjectedSpeedupSkipDetail(
                        metrics,
                        method,
                        projection.speedup_vs_bfs,
                        pivots_after,
                        pivot_total,
                        bfs_total,
                        projection.projected_total_nanoseconds,
                        minimum_speedup
                    );
                    refreshTotalBenchmarkNanoseconds(metrics);
                    skipRemainingWorkForCurrentMethod();
                    return IncrementalClosurePreprocessResult::Skipped;
                }
            }
        }

        if (!finished) {
            return IncrementalClosurePreprocessResult::StepIncomplete;
        }

        metrics.status = baseline.status();
        metrics.preprocess_nanoseconds = live_preprocess;
        metrics.projected_total_speedup_vs_bfs =
            projectedClosureTotalSpeedupVsBfs(
                report,
                config,
                incremental_closure_setup_nanoseconds,
                pivot_elapsed,
                pivots_after,
                pivot_total
            ).speedup_vs_bfs;
        metrics.estimated_index_bytes = indexStorageBytesForBaseline(
            method,
            baselines,
            graph
        );
        const uint64_t rss_after = currentProcessRssBytes();
        if (incremental_closure_rss_before > 0U
            && rss_after >= incremental_closure_rss_before) {
            metrics.preprocess_rss_delta_bytes =
                rss_after - incremental_closure_rss_before;
        }
        progress.preprocess_started_ns = 0U;
        incremental_closure_running = false;
        refreshTotalBenchmarkNanoseconds(metrics);

        if (metrics.status != BaselineStatus::Completed) {
            skipRemainingWorkForCurrentMethod();
            return IncrementalClosurePreprocessResult::Skipped;
        }

        method_phase = MethodPhase::Warmup;
        query_index = 0U;
        progress.stage = ReachabilityBenchmarkProgress::Stage::WarmingUp;
        setStageWork(0U, config.warmup_queries);
        return IncrementalClosurePreprocessResult::CompletedWarmup;
    }

    IncrementalClosurePreprocessResult stepIncrementalSccSearchPreprocess(
        BaselineBenchmarkMetrics& metrics
    ) {
        constexpr ReachabilityBaselineId method = ReachabilityBaselineId::SccDagSearch;

        if (skip_current_method_requested.exchange(false, std::memory_order_acq_rel)) {
            baselines.scc_search.cancelPreprocess();
            incremental_scc_search_running = false;
            metrics.status = BaselineStatus::SkippedByPolicy;
            metrics.policy_skip_detail =
                "Stopped by user during SccDagSearch preprocessing.";
            progress.preprocess_started_ns = 0U;
            skipRemainingWorkForCurrentMethod();
            return IncrementalClosurePreprocessResult::Skipped;
        }

        if (!incremental_scc_search_running) {
            incremental_scc_search_started_ns = BenchTimer::steadyNowNanoseconds();
            progress.preprocess_started_ns = incremental_scc_search_started_ns;
            baselines.scc_search.beginPreprocess(graph, preprocess_scratch);
            incremental_scc_search_running = true;
            const uint64_t work_total = baselines.scc_search.preprocessWorkTotal();
            setStageWork(
                0U,
                static_cast<uint32_t>(std::min<uint64_t>(
                    work_total,
                    std::numeric_limits<uint32_t>::max()
                ))
            );
        }

        const uint64_t work_before = baselines.scc_search.preprocessWorkCompleted();
        const uint32_t vertex_batch = std::max(
            4096U,
            graph.numVertices() / 64U
        );
        const bool finished = baselines.scc_search.preprocessStep(
            preprocess_scratch,
            vertex_batch
        );
        const uint64_t work_after = baselines.scc_search.preprocessWorkCompleted();
        advanceWork(work_after - work_before);
        const uint64_t work_total = baselines.scc_search.preprocessWorkTotal();
        setStageWork(
            static_cast<uint32_t>(std::min<uint64_t>(
                work_after,
                std::numeric_limits<uint32_t>::max()
            )),
            static_cast<uint32_t>(std::min<uint64_t>(
                work_total,
                std::numeric_limits<uint32_t>::max()
            ))
        );

        metrics.preprocess_nanoseconds =
            BenchTimer::steadyNowNanoseconds() - incremental_scc_search_started_ns;
        refreshTotalBenchmarkNanoseconds(metrics);

        if (!finished) {
            return IncrementalClosurePreprocessResult::StepIncomplete;
        }

        incremental_scc_search_running = false;
        progress.preprocess_started_ns = 0U;
        metrics.status = baselines.scc_search.status();
        metrics.estimated_index_bytes = indexStorageBytesForBaseline(
            method,
            baselines,
            graph
        );
        refreshTotalBenchmarkNanoseconds(metrics);

        if (metrics.status != BaselineStatus::Completed) {
            skipRemainingWorkForCurrentMethod();
            return IncrementalClosurePreprocessResult::Skipped;
        }

        method_phase = MethodPhase::Warmup;
        query_index = 0U;
        progress.stage = ReachabilityBenchmarkProgress::Stage::WarmingUp;
        setStageWork(0U, config.warmup_queries);
        return IncrementalClosurePreprocessResult::CompletedWarmup;
    }

    IncrementalClosurePreprocessResult stepIncrementalBrickPreprocess(
        BaselineBenchmarkMetrics& metrics
    ) {
        const ReachabilityBaselineId method = methods[method_index];

        if (skip_current_method_requested.exchange(false, std::memory_order_acq_rel)) {
            if (method == ReachabilityBaselineId::BrickClosure) {
                baselines.brick_closure.cancelPreprocess();
            } else {
                baselines.brick_search.cancelPreprocess();
            }
            incremental_brick_running = false;
            metrics.status = BaselineStatus::SkippedByPolicy;
            metrics.policy_skip_detail =
                std::string("Stopped by user during ")
                + reachabilityBaselineName(method)
                + " preprocessing.";
            progress.preprocess_started_ns = 0U;
            skipRemainingWorkForCurrentMethod();
            return IncrementalClosurePreprocessResult::Skipped;
        }

        const GridBenchmarkContext grid_context = gridContext();
        if (!grid_context.has_grid || grid_context.grid_graph == nullptr
            || grid_context.layout == nullptr) {
            metrics.status = BaselineStatus::SkippedByPolicy;
            metrics.policy_skip_detail =
                "Brick baselines require DirectedGridGraph and MazeLayout benchmark input.";
            skipRemainingWorkForCurrentMethod();
            return IncrementalClosurePreprocessResult::Skipped;
        }

        if (!incremental_brick_running) {
            incremental_brick_rss_before = currentProcessRssBytes();
            incremental_brick_started_ns = BenchTimer::steadyNowNanoseconds();
            progress.preprocess_started_ns = incremental_brick_started_ns;
            if (method == ReachabilityBaselineId::BrickClosure) {
                KleeneSquaringOptions kleene_options{};
                kleene_options.use_parallel = config.kleene_use_parallel;
                kleene_options.num_threads = config.kleene_num_threads;
                baselines.brick_closure.beginPreprocess(
                    *grid_context.grid_graph,
                    *grid_context.layout,
                    config.brick_tile_size,
                    config.max_memory_bytes,
                    kleene_options
                );
            } else {
                baselines.brick_search.beginPreprocess(
                    *grid_context.grid_graph,
                    *grid_context.layout,
                    config.brick_tile_size,
                    config.max_memory_bytes
                );
            }
            incremental_brick_running = true;
            const uint64_t work_total =
                method == ReachabilityBaselineId::BrickClosure
                    ? baselines.brick_closure.preprocessWorkTotal()
                    : baselines.brick_search.preprocessWorkTotal();
            setStageWork(
                0U,
                static_cast<uint32_t>(std::min<uint64_t>(
                    std::max<uint64_t>(work_total, 1ULL),
                    std::numeric_limits<uint32_t>::max()
                ))
            );
        }

        const uint64_t work_before =
            method == ReachabilityBaselineId::BrickClosure
                ? baselines.brick_closure.preprocessWorkCompleted()
                : baselines.brick_search.preprocessWorkCompleted();
        const bool finished =
            method == ReachabilityBaselineId::BrickClosure
                ? baselines.brick_closure.preprocessStep()
                : baselines.brick_search.preprocessStep();
        const uint64_t work_after =
            method == ReachabilityBaselineId::BrickClosure
                ? baselines.brick_closure.preprocessWorkCompleted()
                : baselines.brick_search.preprocessWorkCompleted();
        advanceWork(work_after - work_before);
        const uint64_t work_total =
            method == ReachabilityBaselineId::BrickClosure
                ? baselines.brick_closure.preprocessWorkTotal()
                : baselines.brick_search.preprocessWorkTotal();
        setStageWork(
            static_cast<uint32_t>(std::min<uint64_t>(
                work_after,
                std::numeric_limits<uint32_t>::max()
            )),
            static_cast<uint32_t>(std::min<uint64_t>(
                std::max<uint64_t>(work_total, 1ULL),
                std::numeric_limits<uint32_t>::max()
            ))
        );

        metrics.preprocess_nanoseconds =
            BenchTimer::steadyNowNanoseconds() - incremental_brick_started_ns;
        refreshTotalBenchmarkNanoseconds(metrics);

        if (!finished) {
            return IncrementalClosurePreprocessResult::StepIncomplete;
        }

        incremental_brick_running = false;
        progress.preprocess_started_ns = 0U;
        metrics.status =
            method == ReachabilityBaselineId::BrickClosure
                ? baselines.brick_closure.status()
                : baselines.brick_search.status();
        metrics.estimated_index_bytes = indexStorageBytesForBaseline(
            method,
            baselines,
            graph
        );
        const uint64_t rss_after = currentProcessRssBytes();
        if (incremental_brick_rss_before > 0U && rss_after >= incremental_brick_rss_before) {
            metrics.preprocess_rss_delta_bytes = rss_after - incremental_brick_rss_before;
        }
        refreshTotalBenchmarkNanoseconds(metrics);

        if (method == ReachabilityBaselineId::BrickClosure
            && metrics.status == BaselineStatus::Completed) {
            metrics.warshall_matrix_order =
                baselines.brick_closure.index().ports().numPorts();
            metrics.warshall_pivot_total = metrics.warshall_matrix_order;
            metrics.warshall_pivots_completed = metrics.warshall_pivot_total;
            metrics.kleene_parallel = baselines.brick_closure.kleeneOptions().use_parallel;
            metrics.kleene_thread_count = baselines.brick_closure.kleeneThreadCount();
        }

        if (metrics.status == BaselineStatus::SkippedByPolicy) {
            setMemoryCapSkipDetail(
                metrics,
                method,
                indexStorageBytesForBaseline(method, baselines, graph) > 0U
                    ? indexStorageBytesForBaseline(method, baselines, graph)
                    : estimatedIndexBytesForMethod(method, graph, config),
                config.max_memory_bytes
            );
        }

        if (metrics.status != BaselineStatus::Completed) {
            skipRemainingWorkForCurrentMethod();
            return IncrementalClosurePreprocessResult::Skipped;
        }

        method_phase = MethodPhase::Warmup;
        query_index = 0U;
        progress.stage = ReachabilityBenchmarkProgress::Stage::WarmingUp;
        setStageWork(0U, config.warmup_queries);
        return IncrementalClosurePreprocessResult::CompletedWarmup;
    }
};

namespace {

[[nodiscard]] uint64_t computeWorkTotal(
    const std::span<const ReachabilityBaselineId> methods,
    const uint32_t pair_count,
    const uint32_t num_vertices,
    const ReachabilityBenchmarkConfig& config
) noexcept {
    uint64_t total = 1ULL;
    for (const ReachabilityBaselineId method : methods) {
        total += workUnitsForMethod(method, num_vertices, config, pair_count);
    }

    return total;
}

}  // namespace

ReachabilityBenchmarkJob::ReachabilityBenchmarkJob()
    : impl_(std::make_unique<Impl>()) {}

ReachabilityBenchmarkJob::~ReachabilityBenchmarkJob() = default;

ReachabilityBenchmarkJob::ReachabilityBenchmarkJob(
    ReachabilityBenchmarkJob&&
) noexcept = default;

ReachabilityBenchmarkJob& ReachabilityBenchmarkJob::operator=(
    ReachabilityBenchmarkJob&&
) noexcept = default;

void ReachabilityBenchmarkJob::begin(
    const CsrGraph& graph,
    const std::span<const uint32_t> query_universe,
    ReachabilityBenchmarkConfig config
) {
    impl_ = std::make_unique<Impl>();
    impl_->graph = graph;
    impl_->has_grid_benchmark = false;
    impl_->config = std::move(config);
    if (impl_->config.methods.empty()) {
        impl_->config.methods = defaultMethodList();
    }

    orderMethodsForBenchmark(impl_->config.methods);

    impl_->query_universe.assign(query_universe.begin(), query_universe.end());
    impl_->methods = impl_->config.methods;
    impl_->preprocess_scratch = GraphSearchScratch(graph.numVertices());
    impl_->query_scratch = GraphSearchScratch(graph.numVertices());

    impl_->report = ReachabilityBenchmarkReport{};
    impl_->report.num_vertices = graph.numVertices();
    impl_->report.num_edges = graph.numEdges();
    impl_->report.pair_seed = impl_->config.pair_seed;
    impl_->report.methods.assign(
        impl_->methods.size(),
        BaselineBenchmarkMetrics{}
    );
    for (std::size_t index = 0; index < impl_->methods.size(); ++index) {
        impl_->report.methods[index].method = impl_->methods[index];
    }

    impl_->progress = ReachabilityBenchmarkProgress{};
    impl_->progress.stage = ReachabilityBenchmarkProgress::Stage::GeneratingPairs;
    impl_->progress.methods_total = static_cast<uint32_t>(impl_->methods.size());
    impl_->progress.work_total = computeWorkTotal(
        impl_->methods,
        impl_->config.query_count,
        graph.numVertices(),
        impl_->config
    );
    impl_->progress.queries_total =
        impl_->config.warmup_queries + impl_->config.query_count;

    impl_->phase = JobPhase::GeneratePairs;
    impl_->method_phase = MethodPhase::Preprocess;
    impl_->method_index = 0U;
    impl_->query_index = 0U;
    impl_->correctness_index = 0U;
    impl_->active_job = true;
}

void ReachabilityBenchmarkJob::begin(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const std::span<const uint32_t> query_universe,
    ReachabilityBenchmarkConfig config
) {
    begin(graph.csrGraph(), query_universe, std::move(config));
    if (!impl_ || !impl_->active_job) {
        return;
    }

    impl_->grid_graph = graph;
    impl_->layout = layout;
    impl_->has_grid_benchmark = true;
}

bool ReachabilityBenchmarkJob::active() const noexcept {
    return impl_ && impl_->active_job;
}

void ReachabilityBenchmarkJob::cancel() noexcept {
    if (!impl_ || !impl_->active_job) {
        return;
    }

    impl_->active_job = false;
    impl_->phase = JobPhase::Done;
    impl_->progress.stage = ReachabilityBenchmarkProgress::Stage::Cancelled;
    impl_->progress.preprocess_started_ns = 0U;
    impl_->report.valid = false;
}

void ReachabilityBenchmarkJob::requestSkipCurrentMethod() noexcept {
    if (!impl_ || !impl_->active_job) {
        return;
    }

    impl_->skip_current_method_requested.store(true, std::memory_order_release);
}

bool ReachabilityBenchmarkJob::step() noexcept {
    if (!impl_ || !impl_->active_job) {
        return true;
    }

    switch (impl_->phase) {
        case JobPhase::GeneratePairs: {
            generateQueryPairs(
                impl_->query_universe,
                impl_->config.query_count,
                impl_->config.pair_seed,
                impl_->pairs
            );
            impl_->report.query_pair_count =
                static_cast<uint32_t>(impl_->pairs.size());
            impl_->progress.work_total = computeWorkTotal(
                impl_->methods,
                impl_->report.query_pair_count,
                impl_->graph.numVertices(),
                impl_->config
            );
            impl_->advanceWork(1U);
            impl_->phase = JobPhase::RunMethod;
            impl_->method_index = 0U;
            impl_->method_phase = MethodPhase::Preprocess;
            impl_->progress.stage = ReachabilityBenchmarkProgress::Stage::Preprocessing;
            break;
        }
        case JobPhase::RunMethod: {
            if (impl_->method_index >= impl_->methods.size()) {
                applySpeedups(impl_->report);
                impl_->report.valid = true;
                impl_->phase = JobPhase::Done;
                impl_->active_job = false;
                impl_->progress.stage = ReachabilityBenchmarkProgress::Stage::Finished;
                impl_->setStageWork(1U, 1U);
                if (impl_->progress.work_total > 0U) {
                    impl_->progress.work_completed = impl_->progress.work_total;
                }
                return true;
            }

            const ReachabilityBaselineId method =
                impl_->methods[impl_->method_index];
            BaselineBenchmarkMetrics& metrics =
                impl_->report.methods[impl_->method_index];
            metrics.method = method;
            impl_->progress.current_method = method;

            switch (impl_->method_phase) {
                case MethodPhase::Preprocess: {
                    impl_->progress.stage =
                        ReachabilityBenchmarkProgress::Stage::Preprocessing;

                    if (method == ReachabilityBaselineId::FullClosure) {
                        const auto outcome = impl_->stepIncrementalClosurePreprocess(
                            impl_->baselines.full_closure,
                            metrics,
                            method
                        );
                        if (outcome == Impl::IncrementalClosurePreprocessResult::StepIncomplete) {
                            break;
                        }
                        break;
                    }

                    if (method == ReachabilityBaselineId::SccDagClosure) {
                        const auto outcome = impl_->stepIncrementalClosurePreprocess(
                            impl_->baselines.scc_closure,
                            metrics,
                            method
                        );
                        if (outcome == Impl::IncrementalClosurePreprocessResult::StepIncomplete) {
                            break;
                        }
                        break;
                    }

                    if (method == ReachabilityBaselineId::SccDagSearch) {
                        const auto outcome = impl_->stepIncrementalSccSearchPreprocess(metrics);
                        if (outcome
                            == Impl::IncrementalClosurePreprocessResult::StepIncomplete) {
                            break;
                        }
                        break;
                    }

                    if (usesIncrementalBrickPreprocess(method)) {
                        const auto outcome = impl_->stepIncrementalBrickPreprocess(metrics);
                        if (outcome
                            == Impl::IncrementalClosurePreprocessResult::StepIncomplete) {
                            break;
                        }
                        break;
                    }

                    impl_->setStageWork(0U, 1U);
                    if (baselineBuildsPreprocessIndex(method)) {
                        const uint64_t rss_before = currentProcessRssBytes();
                        impl_->progress.preprocess_started_ns =
                            BenchTimer::steadyNowNanoseconds();
                        BenchTimer timer;
                        timer.start();
                        metrics.status = preprocessBaseline(
                            method,
                            impl_->baselines,
                            impl_->graph,
                            impl_->preprocess_scratch,
                            impl_->config,
                            impl_->gridContext()
                        );
                        timer.stop();
                        impl_->progress.preprocess_started_ns = 0U;
                        metrics.preprocess_nanoseconds = timer.elapsedNanoseconds();
                        metrics.estimated_index_bytes = indexStorageBytesForBaseline(
                            method,
                            impl_->baselines,
                            impl_->graph
                        );
                        const uint64_t rss_after = currentProcessRssBytes();
                        if (rss_before > 0U && rss_after >= rss_before) {
                            metrics.preprocess_rss_delta_bytes = rss_after - rss_before;
                        }
                    } else {
                        metrics.status = preprocessBaseline(
                            method,
                            impl_->baselines,
                            impl_->graph,
                            impl_->preprocess_scratch,
                            impl_->config,
                            impl_->gridContext()
                        );
                        metrics.preprocess_nanoseconds = 0U;
                        metrics.estimated_index_bytes = 0U;
                        metrics.preprocess_rss_delta_bytes = 0U;
                    }

                    impl_->refreshTotalBenchmarkNanoseconds(metrics);
                    impl_->setStageWork(1U, 1U);
                    impl_->advanceWork(1U);

                    if (metrics.status == BaselineStatus::SkippedByPolicy) {
                        if (gridBaselineRequiresMazeContext(method)
                            && !impl_->has_grid_benchmark) {
                            metrics.policy_skip_detail =
                                "Brick and H-BRICK baselines require DirectedGridGraph "
                                "and MazeLayout benchmark input.";
                        } else {
                            const uint64_t actual_index_bytes = indexStorageBytesForBaseline(
                                method,
                                impl_->baselines,
                                impl_->graph
                            );
                            setMemoryCapSkipDetail(
                                metrics,
                                method,
                                actual_index_bytes > 0U
                                    ? actual_index_bytes
                                    : estimatedIndexBytesForMethod(
                                        method,
                                        impl_->graph,
                                        impl_->config
                                    ),
                                impl_->config.max_memory_bytes
                            );
                        }
                    }

                    if (method == ReachabilityBaselineId::BrickClosure
                        && metrics.status == BaselineStatus::Completed) {
                        metrics.warshall_matrix_order =
                            impl_->baselines.brick_closure.index().ports().numPorts();
                        metrics.warshall_pivot_total = metrics.warshall_matrix_order;
                        metrics.warshall_pivots_completed = metrics.warshall_pivot_total;
                        metrics.kleene_parallel =
                            impl_->baselines.brick_closure.kleeneOptions().use_parallel;
                        metrics.kleene_thread_count =
                            impl_->baselines.brick_closure.kleeneThreadCount();
                    }

                    if (metrics.status != BaselineStatus::Completed) {
                        impl_->skipRemainingWorkForCurrentMethod();
                        break;
                    }

                    impl_->method_phase = MethodPhase::Warmup;
                    impl_->query_index = 0U;
                    impl_->progress.stage =
                        ReachabilityBenchmarkProgress::Stage::WarmingUp;
                    impl_->setStageWork(0U, impl_->config.warmup_queries);
                    break;
                }
                case MethodPhase::Warmup: {
                    impl_->progress.stage =
                        ReachabilityBenchmarkProgress::Stage::WarmingUp;

                    if (metrics.status != BaselineStatus::Completed
                        || impl_->pairs.empty()) {
                        impl_->skipRemainingWorkForCurrentMethod();
                        break;
                    }

                    impl_->setStageWork(
                        static_cast<uint32_t>(impl_->query_index),
                        impl_->config.warmup_queries
                    );

                    const uint32_t batch = std::min(
                        impl_->config.queries_per_step,
                        impl_->config.warmup_queries
                            - static_cast<uint32_t>(impl_->query_index)
                    );

                    const uint64_t batch_start = monotonicNowNanoseconds();
                    for (uint32_t step = 0U; step < batch; ++step) {
                        const ReachabilityQueryPair& pair =
                            impl_->pairs[
                                (impl_->query_index + step) % impl_->pairs.size()
                            ];
                        executeWarmupQuery(
                            method,
                            impl_->baselines,
                            pair,
                            impl_->query_scratch
                        );
                    }
                    metrics.warmup_nanoseconds +=
                        monotonicNowNanoseconds() - batch_start;
                    impl_->refreshTotalBenchmarkNanoseconds(metrics);

                    impl_->query_index += batch;
                    impl_->setStageWork(
                        static_cast<uint32_t>(impl_->query_index),
                        impl_->config.warmup_queries
                    );
                    impl_->advanceWork(static_cast<uint64_t>(batch));

                    if (impl_->query_index < impl_->config.warmup_queries) {
                        break;
                    }

                    impl_->method_phase = MethodPhase::Query;
                    impl_->query_index = 0U;
                    impl_->progress.stage =
                        ReachabilityBenchmarkProgress::Stage::Querying;
                    impl_->setStageWork(0U, impl_->config.query_count);
                    break;
                }
                case MethodPhase::Query: {
                    impl_->progress.stage =
                        ReachabilityBenchmarkProgress::Stage::Querying;

                    if (metrics.status != BaselineStatus::Completed
                        || impl_->pairs.empty()) {
                        impl_->skipRemainingWorkForCurrentMethod();
                        break;
                    }

                    if (impl_->query_index == 0U) {
                        impl_->current_query_times.clear();
                        impl_->current_query_times.reserve(impl_->config.query_count);
                        impl_->current_positive_ns_sum = 0.0;
                        impl_->current_negative_ns_sum = 0.0;
                        metrics.reachable_count = 0U;
                        metrics.unreachable_count = 0U;
                        metrics.positive_query_count = 0U;
                        metrics.negative_query_count = 0U;
                        metrics.grail_tree_hits = 0U;
                        metrics.grail_bfs_fallbacks = 0U;
                        metrics.mean_positive_query_nanoseconds = 0.0;
                        metrics.mean_negative_query_nanoseconds = 0.0;
                    }

                    impl_->setStageWork(
                        static_cast<uint32_t>(impl_->query_index),
                        impl_->config.query_count
                    );

                    const uint32_t batch = std::min(
                        impl_->config.queries_per_step,
                        impl_->config.query_count
                            - static_cast<uint32_t>(impl_->query_index)
                    );

                    for (uint32_t step = 0U; step < batch; ++step) {
                        const ReachabilityQueryPair& pair =
                            impl_->pairs[impl_->query_index + step];
                        const QueryExecutionStats execution = executeTimedQuery(
                            method,
                            impl_->baselines,
                            pair,
                            impl_->query_scratch
                        );

                        impl_->current_query_times.push_back(
                            execution.elapsed_nanoseconds
                        );

                        if (execution.answer == ReachabilityAnswer::Reachable) {
                            ++metrics.reachable_count;
                            ++metrics.positive_query_count;
                            impl_->current_positive_ns_sum += static_cast<double>(
                                execution.elapsed_nanoseconds
                            );
                        } else {
                            ++metrics.unreachable_count;
                            ++metrics.negative_query_count;
                            impl_->current_negative_ns_sum += static_cast<double>(
                                execution.elapsed_nanoseconds
                            );
                        }

                        if (method == ReachabilityBaselineId::Grail) {
                            if (execution.grail_tree_certified) {
                                ++metrics.grail_tree_hits;
                            } else {
                                ++metrics.grail_bfs_fallbacks;
                            }
                        }
                    }

                    impl_->query_index += batch;
                    refreshPartialQueryStatistics(
                        impl_->current_query_times,
                        metrics.query_stats
                    );
                    impl_->refreshTotalBenchmarkNanoseconds(metrics);
                    impl_->progress.queries_completed =
                        impl_->config.warmup_queries
                        + static_cast<uint32_t>(impl_->query_index);
                    impl_->setStageWork(
                        static_cast<uint32_t>(impl_->query_index),
                        impl_->config.query_count
                    );
                    impl_->advanceWork(static_cast<uint64_t>(batch));

                    if (impl_->query_index < impl_->config.query_count) {
                        break;
                    }

                    const uint32_t total_answers =
                        metrics.reachable_count + metrics.unreachable_count;
                    if (total_answers > 0U) {
                        metrics.reachable_ratio =
                            static_cast<double>(metrics.reachable_count)
                            / static_cast<double>(total_answers);
                    }

                    if (metrics.positive_query_count > 0U) {
                        metrics.mean_positive_query_nanoseconds =
                            impl_->current_positive_ns_sum
                            / static_cast<double>(metrics.positive_query_count);
                    }
                    if (metrics.negative_query_count > 0U) {
                        metrics.mean_negative_query_nanoseconds =
                            impl_->current_negative_ns_sum
                            / static_cast<double>(metrics.negative_query_count);
                    }

                    finalizeQueryStatistics(
                        impl_->current_query_times,
                        metrics.query_stats
                    );
                    impl_->refreshTotalBenchmarkNanoseconds(metrics);
                    applySpeedups(impl_->report);

                    impl_->method_phase = MethodPhase::Correctness;
                    impl_->correctness_index = 0U;
                    impl_->progress.stage =
                        ReachabilityBenchmarkProgress::Stage::CorrectnessCheck;
                    impl_->setStageWork(
                        0U,
                        std::min(
                            impl_->config.correctness_check_count,
                            static_cast<uint32_t>(impl_->pairs.size())
                        )
                    );
                    break;
                }
                case MethodPhase::Correctness: {
                    const uint32_t checks = std::min(
                        impl_->config.correctness_check_count,
                        static_cast<uint32_t>(impl_->pairs.size())
                    );

                    impl_->progress.stage =
                        ReachabilityBenchmarkProgress::Stage::CorrectnessCheck;

                    if (metrics.status != BaselineStatus::Completed || checks == 0U) {
                        if (checks > 0U) {
                            impl_->advanceWork(static_cast<uint64_t>(checks));
                        }
                        impl_->advanceToNextMethod();
                        if (impl_->method_index < impl_->methods.size()) {
                            impl_->progress.stage =
                                ReachabilityBenchmarkProgress::Stage::Preprocessing;
                        }
                        break;
                    }

                    impl_->setStageWork(
                        static_cast<uint32_t>(impl_->correctness_index),
                        checks
                    );

                    const uint32_t batch = std::min(
                        impl_->config.queries_per_step,
                        checks - static_cast<uint32_t>(impl_->correctness_index)
                    );

                    const uint64_t batch_start = monotonicNowNanoseconds();
                    for (uint32_t step = 0U; step < batch; ++step) {
                        const ReachabilityQueryPair& pair =
                            impl_->pairs[impl_->correctness_index + step];
                        const ReachabilityAnswer expected = referenceBfsAnswer(
                            impl_->graph,
                            pair,
                            impl_->query_scratch
                        );
                        const QueryExecutionStats actual = executeTimedQuery(
                            method,
                            impl_->baselines,
                            pair,
                            impl_->query_scratch
                        );
                        ++metrics.correctness_checks;
                        if (actual.answer != expected) {
                            ++metrics.correctness_mismatches;
                        }
                    }
                    metrics.correctness_nanoseconds +=
                        monotonicNowNanoseconds() - batch_start;

                    impl_->correctness_index += batch;
                    impl_->setStageWork(
                        static_cast<uint32_t>(impl_->correctness_index),
                        checks
                    );
                    impl_->advanceWork(static_cast<uint64_t>(batch));

                    if (impl_->correctness_index < checks) {
                        break;
                    }

                    impl_->advanceToNextMethod();
                    if (impl_->method_index < impl_->methods.size()) {
                        impl_->progress.stage =
                            ReachabilityBenchmarkProgress::Stage::Preprocessing;
                    }
                    break;
                }
            }
            break;
        }
        case JobPhase::Done:
            return true;
    }

    return false;
}

ReachabilityBenchmarkProgress ReachabilityBenchmarkJob::progress() const noexcept {
    if (!impl_) {
        return ReachabilityBenchmarkProgress{};
    }

    return impl_->progress;
}

const ReachabilityBenchmarkReport& ReachabilityBenchmarkJob::report() const noexcept {
    static const ReachabilityBenchmarkReport kEmpty{};
    if (!impl_) {
        return kEmpty;
    }

    return impl_->report;
}

ReachabilityBenchmarkReport ReachabilityBenchmarkJob::run(
    const CsrGraph& graph,
    const std::span<const uint32_t> query_universe,
    ReachabilityBenchmarkConfig config
) {
    ReachabilityBenchmarkJob job;
    job.begin(graph, query_universe, std::move(config));
    while (!job.step()) {
    }
    return job.report();
}

ReachabilityBenchmarkReport ReachabilityBenchmarkJob::run(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const std::span<const uint32_t> query_universe,
    ReachabilityBenchmarkConfig config
) {
    ReachabilityBenchmarkJob job;
    job.begin(graph, layout, query_universe, std::move(config));
    while (!job.step()) {
    }
    return job.report();
}

}  // namespace hbrick
