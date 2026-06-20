#include "hbrick/bench/reachability_benchmark_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "hbrick/bench/bench_timer.hpp"
#include "hbrick/bench/reachability_benchmark_util.hpp"

namespace hbrick {

void formatBenchmarkNanoseconds(
    char* buffer,
    const std::size_t size,
    const double nanoseconds
) noexcept {
    if (nanoseconds >= 1.0e9) {
        std::snprintf(buffer, size, "%.2f s", nanoseconds / 1.0e9);
    } else if (nanoseconds >= 1.0e6) {
        std::snprintf(buffer, size, "%.2f ms", nanoseconds / 1.0e6);
    } else if (nanoseconds >= 1.0e3) {
        std::snprintf(buffer, size, "%.2f us", nanoseconds / 1.0e3);
    } else {
        std::snprintf(buffer, size, "%.0f ns", nanoseconds);
    }
}

void formatBenchmarkBytes(char* buffer, const std::size_t size, const uint64_t bytes) noexcept {
    if (bytes >= 1ULL << 40) {
        std::snprintf(
            buffer,
            size,
            "%.2f TiB",
            static_cast<double>(bytes) / static_cast<double>(1ULL << 40)
        );
    } else if (bytes >= 1ULL << 30) {
        std::snprintf(
            buffer,
            size,
            "%.2f GiB",
            static_cast<double>(bytes) / static_cast<double>(1ULL << 30)
        );
    } else if (bytes >= 1ULL << 20) {
        std::snprintf(
            buffer,
            size,
            "%.2f MiB",
            static_cast<double>(bytes) / static_cast<double>(1ULL << 20)
        );
    } else if (bytes >= 1ULL << 10) {
        std::snprintf(
            buffer,
            size,
            "%.2f KiB",
            static_cast<double>(bytes) / static_cast<double>(1ULL << 10)
        );
    } else {
        std::snprintf(buffer, size, "%llu B", static_cast<unsigned long long>(bytes));
    }
}

void formatBenchmarkLargeCount(
    char* buffer,
    const std::size_t size,
    const uint64_t value
) noexcept {
    if (value >= 1'000'000'000'000ULL) {
        std::snprintf(
            buffer,
            size,
            "%.2f T",
            static_cast<double>(value) / 1'000'000'000'000.0
        );
    } else if (value >= 1'000'000'000ULL) {
        std::snprintf(
            buffer,
            size,
            "%.2f B",
            static_cast<double>(value) / 1'000'000'000.0
        );
    } else if (value >= 1'000'000ULL) {
        std::snprintf(
            buffer,
            size,
            "%.2f M",
            static_cast<double>(value) / 1'000'000.0
        );
    } else if (value >= 10'000ULL) {
        std::snprintf(
            buffer,
            size,
            "%.2f K",
            static_cast<double>(value) / 1'000.0
        );
    } else {
        std::snprintf(buffer, size, "%llu", static_cast<unsigned long long>(value));
    }
}

void formatBenchmarkLargeCountPair(
    char* buffer,
    const std::size_t size,
    const uint64_t completed,
    const uint64_t total
) noexcept {
    char completed_label[24];
    char total_label[24];
    formatBenchmarkLargeCount(completed_label, sizeof(completed_label), completed);
    formatBenchmarkLargeCount(total_label, sizeof(total_label), total);
    std::snprintf(buffer, size, "%s / %s", completed_label, total_label);
}

void formatBenchmarkSpeedupRatio(
    char* buffer,
    const std::size_t size,
    const double speedup
) noexcept {
    if (speedup <= 0.0 || !std::isfinite(speedup)) {
        std::snprintf(buffer, size, "-");
        return;
    }

    if (speedup >= 0.01) {
        std::snprintf(buffer, size, "%.2fx", speedup);
        return;
    }

    if (speedup >= 0.0001) {
        std::snprintf(buffer, size, "%.4fx", speedup);
        return;
    }

    std::snprintf(buffer, size, "<0.0001x");
}

const char* reachabilityBenchmarkStageLabel(
    const ReachabilityBenchmarkProgress::Stage stage
) noexcept {
    using Stage = ReachabilityBenchmarkProgress::Stage;
    switch (stage) {
        case Stage::Idle:
            return "Idle";
        case Stage::GeneratingPairs:
            return "Generating query pairs";
        case Stage::Preprocessing:
            return "Preprocessing";
        case Stage::WarmingUp:
            return "Warmup queries";
        case Stage::Querying:
            return "Timed queries";
        case Stage::CorrectnessCheck:
            return "Correctness checks";
        case Stage::Finished:
            return "Finished";
        case Stage::Cancelled:
            return "Cancelled";
    }

    return "Unknown";
}

void formatReachabilityBenchmarkStageDetail(
    char* buffer,
    const std::size_t size,
    const ReachabilityBenchmarkProgress& progress
) noexcept {
    const char* method_name = reachabilityBaselineName(progress.current_method);

    switch (progress.stage) {
        case ReachabilityBenchmarkProgress::Stage::GeneratingPairs:
            std::snprintf(buffer, size, "Generating shared query pairs");
            break;
        case ReachabilityBenchmarkProgress::Stage::Preprocessing:
            if (progress.current_method == ReachabilityBaselineId::SccDagClosure
                || progress.current_method == ReachabilityBaselineId::FullClosure) {
                std::snprintf(
                    buffer,
                    size,
                    "Preprocessing %s  Warshall k=%u/%u  (method %u / %u)",
                    method_name,
                    progress.stage_work_completed,
                    progress.stage_work_total,
                    std::min(progress.methods_completed + 1U, progress.methods_total),
                    progress.methods_total
                );
            } else {
                std::snprintf(
                    buffer,
                    size,
                    "Preprocessing %s  (method %u / %u)",
                    method_name,
                    std::min(progress.methods_completed + 1U, progress.methods_total),
                    progress.methods_total
                );
            }
            break;
        case ReachabilityBenchmarkProgress::Stage::WarmingUp:
            std::snprintf(
                buffer,
                size,
                "Warmup on %s  %u / %u",
                method_name,
                progress.stage_work_completed,
                progress.stage_work_total
            );
            break;
        case ReachabilityBenchmarkProgress::Stage::Querying:
            std::snprintf(
                buffer,
                size,
                "Timed queries on %s  %u / %u",
                method_name,
                progress.stage_work_completed,
                progress.stage_work_total
            );
            break;
        case ReachabilityBenchmarkProgress::Stage::CorrectnessCheck:
            std::snprintf(
                buffer,
                size,
                "Correctness checks on %s  %u / %u",
                method_name,
                progress.stage_work_completed,
                progress.stage_work_total
            );
            break;
        default:
            std::snprintf(buffer, size, "%s", reachabilityBenchmarkStageLabel(progress.stage));
            break;
    }
}

void formatReachabilityBenchmarkPreprocessCell(
    char* buffer,
    const std::size_t size,
    const BaselineBenchmarkMetrics& metrics,
    const ReachabilityBenchmarkReport& report,
    const ReachabilityBenchmarkProgress& progress,
    const std::size_t method_index,
    const bool running
) noexcept {
    using Stage = ReachabilityBenchmarkProgress::Stage;
    const int active_index =
        activeReachabilityBenchmarkMethodIndex(report, progress, running);
    if (running
        && progress.stage == Stage::Preprocessing
        && static_cast<int>(method_index) == active_index
        && metrics.preprocess_nanoseconds > 0U) {
        formatBenchmarkNanoseconds(
            buffer,
            size,
            static_cast<double>(metrics.preprocess_nanoseconds)
        );
        return;
    }

    if (running
        && progress.stage == Stage::Preprocessing
        && static_cast<int>(method_index) == active_index
        && progress.preprocess_started_ns > 0U) {
        const uint64_t now = BenchTimer::steadyNowNanoseconds();
        formatBenchmarkNanoseconds(
            buffer,
            size,
            static_cast<double>(now - progress.preprocess_started_ns)
        );
        return;
    }

    if (!baselineBuildsPreprocessIndex(metrics.method)
        && metrics.status != BaselineStatus::NotRun) {
        std::snprintf(buffer, size, "none");
        return;
    }

    if (metrics.preprocess_nanoseconds > 0U
        || metrics.status != BaselineStatus::NotRun) {
        formatBenchmarkNanoseconds(
            buffer,
            size,
            static_cast<double>(metrics.preprocess_nanoseconds)
        );
        return;
    }

    std::snprintf(buffer, size, "-");
}

}  // namespace hbrick
