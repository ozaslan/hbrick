#include "hbrick/bench/reachability_benchmark_util.hpp"

namespace hbrick {

namespace {

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

[[nodiscard]] uint64_t fnv1aUpdate(const uint64_t hash, const uint8_t byte) noexcept {
    return (hash ^ static_cast<uint64_t>(byte)) * kFnvPrime;
}

[[nodiscard]] uint64_t fnv1aUpdateUint32(uint64_t hash, const uint32_t value) noexcept {
    for (int shift = 0; shift < 32; shift += 8) {
        hash = fnv1aUpdate(hash, static_cast<uint8_t>((value >> shift) & 0xFFU));
    }
    return hash;
}

}  // namespace

uint64_t hashReachabilityQueryPairs(
    const std::span<const ReachabilityQueryPair> pairs
) noexcept {
    uint64_t hash = kFnvOffsetBasis;
    hash = fnv1aUpdateUint32(hash, static_cast<uint32_t>(pairs.size()));
    for (const ReachabilityQueryPair& pair : pairs) {
        hash = fnv1aUpdateUint32(hash, pair.source);
        hash = fnv1aUpdateUint32(hash, pair.target);
    }
    return hash;
}

bool baselineBuildsPreprocessIndex(const ReachabilityBaselineId method) noexcept {
    switch (method) {
        case ReachabilityBaselineId::CsrBfs:
        case ReachabilityBaselineId::CsrDfs:
            return false;
        case ReachabilityBaselineId::SccDagSearch:
        case ReachabilityBaselineId::SccDagClosure:
        case ReachabilityBaselineId::FullClosure:
        case ReachabilityBaselineId::TwoHop:
        case ReachabilityBaselineId::Grail:
        case ReachabilityBaselineId::BrickSearch:
        case ReachabilityBaselineId::BrickClosure:
        case ReachabilityBaselineId::HBrick:
            return true;
    }

    return true;
}

int activeReachabilityBenchmarkMethodIndex(
    const ReachabilityBenchmarkReport& report,
    const ReachabilityBenchmarkProgress& progress,
    const bool running
) noexcept {
    if (!running || report.methods.empty()) {
        return -1;
    }

    using Stage = ReachabilityBenchmarkProgress::Stage;
    switch (progress.stage) {
        case Stage::Preprocessing:
        case Stage::WarmingUp:
        case Stage::Querying:
        case Stage::CorrectnessCheck:
            for (std::size_t method_index = 0; method_index < report.methods.size();
                 ++method_index) {
                if (report.methods[method_index].method == progress.current_method) {
                    return static_cast<int>(method_index);
                }
            }
            return -1;
        case Stage::GeneratingPairs:
        case Stage::Idle:
        case Stage::Finished:
        case Stage::Cancelled:
            return -1;
    }

    return -1;
}

bool closurePreprocessActive(
    const ReachabilityBenchmarkProgress& progress,
    const bool running
) noexcept {
    if (!running) {
        return false;
    }

    using Stage = ReachabilityBenchmarkProgress::Stage;
    if (progress.stage != Stage::Preprocessing) {
        return false;
    }

    return progress.current_method == ReachabilityBaselineId::SccDagClosure
        || progress.current_method == ReachabilityBaselineId::FullClosure
        || progress.current_method == ReachabilityBaselineId::BrickClosure;
}

uint64_t referenceBfsTotalNanoseconds(
    const ReachabilityBenchmarkReport& report
) noexcept {
    if (report.reference_bfs_total_benchmark_nanoseconds > 0U) {
        return report.reference_bfs_total_benchmark_nanoseconds;
    }

    for (const BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.method == ReachabilityBaselineId::CsrBfs
            && metrics.total_benchmark_nanoseconds > 0U) {
            return metrics.total_benchmark_nanoseconds;
        }
    }

    return 0U;
}

double referenceBfsMeanQueryNanoseconds(
    const ReachabilityBenchmarkReport& report
) noexcept {
    if (report.reference_bfs_mean_query_nanoseconds > 0.0) {
        return report.reference_bfs_mean_query_nanoseconds;
    }

    for (const BaselineBenchmarkMetrics& metrics : report.methods) {
        if (metrics.method == ReachabilityBaselineId::CsrBfs
            && metrics.query_stats.count > 0U
            && metrics.query_stats.mean_nanoseconds > 0.0) {
            return metrics.query_stats.mean_nanoseconds;
        }
    }

    return 0.0;
}

double liveQuerySpeedupVsBfs(
    const ReachabilityBenchmarkReport& report,
    const BaselineBenchmarkMetrics& metrics
) noexcept {
    if (metrics.speedup_vs_bfs > 0.0) {
        return metrics.speedup_vs_bfs;
    }

    if (metrics.query_stats.count == 0U || metrics.query_stats.mean_nanoseconds <= 0.0) {
        return 0.0;
    }

    if (metrics.method == ReachabilityBaselineId::CsrBfs) {
        return 1.0;
    }

    const double reference_mean = referenceBfsMeanQueryNanoseconds(report);
    if (reference_mean <= 0.0) {
        return 0.0;
    }

    return reference_mean / metrics.query_stats.mean_nanoseconds;
}

}  // namespace hbrick
