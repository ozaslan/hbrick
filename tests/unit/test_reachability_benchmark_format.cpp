#include <gtest/gtest.h>

#include <string>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/bench/reachability_benchmark_format.hpp"
#include "hbrick/bench/reachability_benchmark_util.hpp"

TEST(ReachabilityBenchmarkFormat, FormatsBytesWithTiers) {
    char buffer[32];
    hbrick::formatBenchmarkBytes(buffer, sizeof(buffer), 1ULL << 10);
    EXPECT_STREQ(buffer, "1.00 KiB");

    hbrick::formatBenchmarkBytes(buffer, sizeof(buffer), 5ULL << 20);
    EXPECT_STREQ(buffer, "5.00 MiB");
}

TEST(ReachabilityBenchmarkFormat, FormatsLargeCounts) {
    char buffer[32];
    hbrick::formatBenchmarkLargeCount(buffer, sizeof(buffer), 12'345ULL);
    EXPECT_STREQ(buffer, "12.35 K");

    hbrick::formatBenchmarkLargeCountPair(buffer, sizeof(buffer), 1000ULL, 2'000'000ULL);
    EXPECT_STREQ(buffer, "1000 / 2.00 M");
}

TEST(ReachabilityBenchmarkFormat, FormatsStageDetailForQuerying) {
    hbrick::ReachabilityBenchmarkProgress progress;
    progress.stage = hbrick::ReachabilityBenchmarkProgress::Stage::Querying;
    progress.current_method = hbrick::ReachabilityBaselineId::CsrBfs;
    progress.stage_work_completed = 4U;
    progress.stage_work_total = 16U;

    char buffer[160];
    hbrick::formatReachabilityBenchmarkStageDetail(buffer, sizeof(buffer), progress);
    EXPECT_NE(std::string(buffer).find("Timed queries on CsrBfs"), std::string::npos);
    EXPECT_NE(std::string(buffer).find("4 / 16"), std::string::npos);
}

TEST(ReachabilityBenchmarkUtil, ChooseQueriesPerStepScalesWithWorkload) {
    EXPECT_EQ(hbrick::chooseBenchmarkQueriesPerStep(512U), 16U);
    EXPECT_EQ(hbrick::chooseBenchmarkQueriesPerStep(50'000U), 128U);
    EXPECT_EQ(hbrick::chooseBenchmarkQueriesPerStep(2'000'000U), 4096U);
}

TEST(ReachabilityBenchmarkUtil, LiveQuerySpeedupUsesPartialCsrBfsStats) {
    hbrick::ReachabilityBenchmarkReport report;
    report.methods.push_back(hbrick::BaselineBenchmarkMetrics{});
    report.methods.back().method = hbrick::ReachabilityBaselineId::CsrBfs;
    report.methods.back().status = hbrick::BaselineStatus::Completed;
    report.methods.back().query_stats.count = 10U;
    report.methods.back().query_stats.mean_nanoseconds = 100.0;

    hbrick::BaselineBenchmarkMetrics dfs_metrics;
    dfs_metrics.method = hbrick::ReachabilityBaselineId::CsrDfs;
    dfs_metrics.status = hbrick::BaselineStatus::Completed;
    dfs_metrics.query_stats.count = 10U;
    dfs_metrics.query_stats.mean_nanoseconds = 50.0;

    EXPECT_NEAR(hbrick::liveQuerySpeedupVsBfs(report, dfs_metrics), 2.0, 1.0e-9);
}

TEST(ReachabilityBenchmarkUtil, BaselineBuildsPreprocessIndexMatchesExpectations) {
    EXPECT_FALSE(hbrick::baselineBuildsPreprocessIndex(hbrick::ReachabilityBaselineId::CsrBfs));
    EXPECT_TRUE(hbrick::baselineBuildsPreprocessIndex(hbrick::ReachabilityBaselineId::TwoHop));
}

TEST(ReachabilityBenchmarkFormat, FormatsNanosecondsWithUnits) {
    char buffer[32];
    hbrick::formatBenchmarkNanoseconds(buffer, sizeof(buffer), 1.5e9);
    EXPECT_STREQ(buffer, "1.50 s");

    hbrick::formatBenchmarkNanoseconds(buffer, sizeof(buffer), 2500.0);
    EXPECT_STREQ(buffer, "2.50 us");

    hbrick::formatBenchmarkNanoseconds(buffer, sizeof(buffer), 500.0);
    EXPECT_STREQ(buffer, "500 ns");
}

TEST(ReachabilityBenchmarkFormat, FormatsSpeedupRatioWithAdaptivePrecision) {
    char buffer[32];
    hbrick::formatBenchmarkSpeedupRatio(buffer, sizeof(buffer), 2.0);
    EXPECT_STREQ(buffer, "2.00x");

    hbrick::formatBenchmarkSpeedupRatio(buffer, sizeof(buffer), 0.0);
    EXPECT_STREQ(buffer, "-");

    hbrick::formatBenchmarkSpeedupRatio(buffer, sizeof(buffer), 0.00005);
    EXPECT_STREQ(buffer, "<0.0001x");
}

TEST(ReachabilityBenchmarkFormat, FormatsPreprocessCellForIndexlessBaseline) {
    hbrick::ReachabilityBenchmarkReport report;
    report.methods.push_back(hbrick::BaselineBenchmarkMetrics{});
    report.methods.back().method = hbrick::ReachabilityBaselineId::CsrBfs;
    report.methods.back().status = hbrick::BaselineStatus::Completed;
    report.methods.back().preprocess_nanoseconds = 0U;

    hbrick::ReachabilityBenchmarkProgress progress;
    char buffer[32];
    hbrick::formatReachabilityBenchmarkPreprocessCell(
        buffer,
        sizeof(buffer),
        report.methods.front(),
        report,
        progress,
        0U,
        false
    );
    EXPECT_STREQ(buffer, "none");
}

TEST(ReachabilityBenchmarkFormat, FormatsPreprocessCellForCompletedIndexBaseline) {
    hbrick::ReachabilityBenchmarkReport report;
    report.methods.push_back(hbrick::BaselineBenchmarkMetrics{});
    report.methods.back().method = hbrick::ReachabilityBaselineId::TwoHop;
    report.methods.back().status = hbrick::BaselineStatus::Completed;
    report.methods.back().preprocess_nanoseconds = 1'500'000ULL;

    hbrick::ReachabilityBenchmarkProgress progress;
    char buffer[32];
    hbrick::formatReachabilityBenchmarkPreprocessCell(
        buffer,
        sizeof(buffer),
        report.methods.front(),
        report,
        progress,
        0U,
        false
    );
    EXPECT_STREQ(buffer, "1.50 ms");
}

TEST(ReachabilityBenchmarkUtil, ReferenceBfsTotalUsesStoredReferenceOrCsrBfsMetrics) {
    hbrick::ReachabilityBenchmarkReport report;
    report.reference_bfs_total_benchmark_nanoseconds = 999U;
    EXPECT_EQ(hbrick::referenceBfsTotalNanoseconds(report), 999U);

    report.reference_bfs_total_benchmark_nanoseconds = 0U;
    report.methods.push_back(hbrick::BaselineBenchmarkMetrics{});
    report.methods.back().method = hbrick::ReachabilityBaselineId::CsrBfs;
    report.methods.back().total_benchmark_nanoseconds = 42U;
    EXPECT_EQ(hbrick::referenceBfsTotalNanoseconds(report), 42U);
}

TEST(ReachabilityBenchmarkUtil, ReferenceBfsMeanUsesStoredReferenceOrCsrBfsMetrics) {
    hbrick::ReachabilityBenchmarkReport report;
    report.reference_bfs_mean_query_nanoseconds = 12.5;
    EXPECT_DOUBLE_EQ(hbrick::referenceBfsMeanQueryNanoseconds(report), 12.5);

    report.reference_bfs_mean_query_nanoseconds = 0.0;
    report.methods.push_back(hbrick::BaselineBenchmarkMetrics{});
    report.methods.back().method = hbrick::ReachabilityBaselineId::CsrBfs;
    report.methods.back().query_stats.count = 8U;
    report.methods.back().query_stats.mean_nanoseconds = 75.0;
    EXPECT_DOUBLE_EQ(hbrick::referenceBfsMeanQueryNanoseconds(report), 75.0);
}

TEST(ReachabilityBenchmarkUtil, ActiveMethodIndexTracksCurrentMethodWhileRunning) {
    hbrick::ReachabilityBenchmarkReport report;
    report.methods.push_back(hbrick::BaselineBenchmarkMetrics{});
    report.methods.front().method = hbrick::ReachabilityBaselineId::CsrBfs;
    report.methods.push_back(hbrick::BaselineBenchmarkMetrics{});
    report.methods.back().method = hbrick::ReachabilityBaselineId::CsrDfs;

    hbrick::ReachabilityBenchmarkProgress progress;
    progress.stage = hbrick::ReachabilityBenchmarkProgress::Stage::Querying;
    progress.current_method = hbrick::ReachabilityBaselineId::CsrDfs;

    EXPECT_EQ(hbrick::activeReachabilityBenchmarkMethodIndex(report, progress, true), 1);
    EXPECT_EQ(hbrick::activeReachabilityBenchmarkMethodIndex(report, progress, false), -1);
    progress.stage = hbrick::ReachabilityBenchmarkProgress::Stage::Finished;
    EXPECT_EQ(hbrick::activeReachabilityBenchmarkMethodIndex(report, progress, true), -1);
}

TEST(ReachabilityBenchmarkUtil, ClosurePreprocessActiveOnlyDuringClosurePreprocess) {
    hbrick::ReachabilityBenchmarkProgress progress;
    progress.stage = hbrick::ReachabilityBenchmarkProgress::Stage::Preprocessing;
    progress.current_method = hbrick::ReachabilityBaselineId::SccDagClosure;

    EXPECT_TRUE(hbrick::closurePreprocessActive(progress, true));
    EXPECT_FALSE(hbrick::closurePreprocessActive(progress, false));

    progress.current_method = hbrick::ReachabilityBaselineId::CsrBfs;
    EXPECT_FALSE(hbrick::closurePreprocessActive(progress, true));

    progress.current_method = hbrick::ReachabilityBaselineId::FullClosure;
    progress.stage = hbrick::ReachabilityBenchmarkProgress::Stage::Querying;
    EXPECT_FALSE(hbrick::closurePreprocessActive(progress, true));
}
