#include "hbrick/bench/benchmark_campaign.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/bench/process_memory.hpp"
#include "hbrick/bench/reachability_benchmark_format.hpp"

#ifndef HBRICK_COMPILER_ID
#define HBRICK_COMPILER_ID "unknown"
#endif

#ifndef HBRICK_COMPILER_VERSION
#define HBRICK_COMPILER_VERSION "unknown"
#endif

#if defined(HBRICK_SOURCE_DIR)
#define HBRICK_CAMPAIGN_STRINGIFY2(x) #x
#define HBRICK_CAMPAIGN_STRINGIFY(x) HBRICK_CAMPAIGN_STRINGIFY2(x)
#endif

namespace hbrick {

namespace {

[[nodiscard]] std::string readPipeFirstLine(const char* command) {
    std::string output;
    FILE* pipe = popen(command, "r");
    if (pipe == nullptr) {
        return output;
    }
    char buffer[512];
    if (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output = buffer;
        while (!output.empty()
            && (output.back() == '\n' || output.back() == '\r')) {
            output.pop_back();
        }
    }
    pclose(pipe);
    return output;
}

[[nodiscard]] std::string currentUtcTimestamp() {
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::time_t time_value = std::chrono::system_clock::to_time_t(now);
    std::tm utc_time{};
#if defined(_WIN32)
    gmtime_s(&utc_time, &time_value);
#else
    gmtime_r(&time_value, &utc_time);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

[[nodiscard]] std::string csvEscape(std::string value) {
    bool needs_quotes = false;
    for (const char character : value) {
        if (character == ',' || character == '"' || character == '\n') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return value;
    }
    std::string escaped;
    escaped.reserve(value.size() + 2U);
    escaped.push_back('"');
    for (const char character : value) {
        if (character == '"') {
            escaped.append("\"\"");
        } else {
            escaped.push_back(character);
        }
    }
    escaped.push_back('"');
    return escaped;
}

[[nodiscard]] bool writeTextFile(
    const std::filesystem::path& path,
    const std::string& contents,
    std::string& error_message
) {
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        error_message = "Failed to write " + path.string();
        return false;
    }
    output << contents;
    if (!output.good()) {
        error_message = "Failed to write " + path.string();
        return false;
    }
    return true;
}

[[nodiscard]] bool fileExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

[[nodiscard]] std::string campaignResultsCsvHeader() {
    return "campaign_schema_version,campaign_id,run_timestamp_utc,map_id,generator_type,"
           "method,status,policy_skip_detail,num_vertices,num_edges,pair_seed,pair_list_hash,"
           "query_count,warmup_queries,correctness_check_count,preprocess_ns,"
           "estimated_index_bytes,peak_preprocess_rss_bytes,query_count_timed,"
           "query_mean_ns,query_median_ns,query_p95_ns,queries_per_second,"
           "total_benchmark_ns,speedup_vs_bfs,total_speedup_vs_bfs,"
           "correctness_checks,correctness_mismatches,warshall_matrix_order,"
           "kleene_parallel,kleene_thread_count\n";
}

[[nodiscard]] std::string campaignManifestCsvHeader() {
    return "campaign_schema_version,campaign_id,map_id,generator_type,recipe_path,"
           "gallery_image_path,num_vertices,num_edges\n";
}

[[nodiscard]] std::string formatResultCsvRow(const BenchmarkCampaignResultRow& row) {
    std::ostringstream stream;
    stream << kBenchmarkCampaignSchemaVersion << ','
           << csvEscape(row.campaign_id) << ','
           << csvEscape(row.run_timestamp_utc) << ','
           << csvEscape(row.map.map_id) << ','
           << csvEscape(row.map.generator_type) << ','
           << reachabilityBaselineName(row.metrics.method) << ','
           << baselineStatusLabel(row.metrics.status) << ','
           << csvEscape(row.metrics.policy_skip_detail) << ','
           << row.num_vertices << ','
           << row.num_edges << ','
           << row.workload.pair_seed << ','
           << row.workload.pair_list_hash << ','
           << row.workload.query_count << ','
           << row.workload.warmup_queries << ','
           << row.workload.correctness_check_count << ','
           << row.metrics.preprocess_nanoseconds << ','
           << row.metrics.estimated_index_bytes << ','
           << row.peak_preprocess_rss_bytes << ','
           << row.metrics.query_stats.count << ','
           << row.metrics.query_stats.mean_nanoseconds << ','
           << row.metrics.query_stats.median_nanoseconds << ','
           << row.metrics.query_stats.p95_nanoseconds << ','
           << row.metrics.query_stats.queries_per_second << ','
           << row.metrics.total_benchmark_nanoseconds << ','
           << row.metrics.speedup_vs_bfs << ','
           << row.metrics.total_speedup_vs_bfs << ','
           << row.metrics.correctness_checks << ','
           << row.metrics.correctness_mismatches << ','
           << row.metrics.warshall_matrix_order << ','
           << (row.metrics.kleene_parallel ? "true" : "false") << ','
           << row.metrics.kleene_thread_count << '\n';
    return stream.str();
}

[[nodiscard]] bool writeMetadataJson(
    const std::filesystem::path& metadata_json,
    const BenchmarkCampaignMetadata& metadata,
    std::string& error_message
) {
    std::ostringstream stream;
    stream << "{\n"
           << "  \"schema_version\": \"" << metadata.schema_version << "\",\n"
           << "  \"campaign_id\": \"" << metadata.campaign_id << "\",\n"
           << "  \"started_at_utc\": \"" << metadata.started_at_utc << "\",\n"
           << "  \"git_commit\": \"" << metadata.git_commit << "\",\n"
           << "  \"build_type\": \"" << metadata.build_type << "\",\n"
           << "  \"compiler_id\": \"" << metadata.compiler_id << "\",\n"
           << "  \"compiler_version\": \"" << metadata.compiler_version << "\",\n"
           << "  \"cpu_model\": \"" << metadata.cpu_model << "\",\n"
           << "  \"hardware_concurrency\": " << metadata.hardware_concurrency << "\n"
           << "}\n";
    return writeTextFile(metadata_json, stream.str(), error_message);
}

}  // namespace

BenchmarkCampaignPaths benchmarkCampaignPathsFromRoot(
    const std::filesystem::path& campaign_root
) {
    BenchmarkCampaignPaths paths{};
    paths.root = campaign_root;
    paths.manifest_csv = campaign_root / "manifest.csv";
    paths.results_csv = campaign_root / "results.csv";
    paths.summary_md = campaign_root / "summary.md";
    paths.metadata_json = campaign_root / "metadata.json";
    paths.workload_json = campaign_root / "workload.json";
    paths.gallery_dir = campaign_root / "gallery";
    paths.recipes_dir = campaign_root / "recipes";
    paths.logs_dir = campaign_root / "logs";
    return paths;
}

bool initializeBenchmarkCampaignDirectory(
    const BenchmarkCampaignPaths& paths,
    BenchmarkCampaignMetadata metadata,
    std::string& error_message
) {
    std::error_code error;
    if (!std::filesystem::create_directories(paths.root, error) && error) {
        error_message = "Failed to create campaign root: " + error.message();
        return false;
    }
    for (const std::filesystem::path& directory :
         {paths.gallery_dir, paths.recipes_dir, paths.logs_dir}) {
        if (!std::filesystem::create_directories(directory, error) && error) {
            error_message = "Failed to create directory: " + error.message();
            return false;
        }
    }

    if (metadata.started_at_utc.empty()) {
        metadata.started_at_utc = currentUtcTimestamp();
    }
    if (!writeMetadataJson(paths.metadata_json, metadata, error_message)) {
        return false;
    }
    if (!writeTextFile(paths.manifest_csv, campaignManifestCsvHeader(), error_message)) {
        return false;
    }
    if (!writeTextFile(paths.results_csv, campaignResultsCsvHeader(), error_message)) {
        return false;
    }

    std::ostringstream summary;
    summary << "# Benchmark campaign\n\n"
            << "Campaign `" << metadata.campaign_id << "` initialized at "
            << metadata.started_at_utc << ".\n\n"
            << "Schema version: `" << metadata.schema_version << "`.\n\n"
            << "| Artifact | Purpose |\n"
            << "|----------|----------|\n"
            << "| `manifest.csv` | Map catalog |\n"
            << "| `results.csv` | Per (map, method) metrics |\n"
            << "| `workload.json` | Shared query workload |\n"
            << "| `metadata.json` | Build and host metadata |\n"
            << "| `gallery/` | Map images |\n"
            << "| `recipes/` | Orientation recipes |\n"
            << "| `logs/` | Run logs |\n";
    return writeTextFile(paths.summary_md, summary.str(), error_message);
}

BenchmarkCampaignMetadata captureBenchmarkCampaignMetadata(std::string campaign_id) {
    BenchmarkCampaignMetadata metadata{};
    metadata.campaign_id = std::move(campaign_id);
    metadata.started_at_utc = currentUtcTimestamp();
#ifdef NDEBUG
    metadata.build_type = "Release";
#else
    metadata.build_type = "Debug";
#endif
    metadata.compiler_id = HBRICK_COMPILER_ID;
    metadata.compiler_version = HBRICK_COMPILER_VERSION;
    metadata.hardware_concurrency =
        static_cast<uint32_t>(std::thread::hardware_concurrency());

#if defined(HBRICK_SOURCE_DIR)
    const std::string git_command = std::string("git -C ")
        + HBRICK_CAMPAIGN_STRINGIFY(HBRICK_SOURCE_DIR)
        + " rev-parse HEAD 2>/dev/null";
    metadata.git_commit = readPipeFirstLine(git_command.c_str());
#endif
    if (metadata.git_commit.empty()) {
        metadata.git_commit = "unknown";
    }

    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo.is_open()) {
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.rfind("model name", 0) == 0U) {
                const std::size_t colon = line.find(':');
                if (colon != std::string::npos && colon + 2U < line.size()) {
                    metadata.cpu_model = line.substr(colon + 2U);
                }
                break;
            }
        }
    }
    if (metadata.cpu_model.empty()) {
        metadata.cpu_model = "unknown";
    }
    return metadata;
}

BenchmarkCampaignQueryWorkload workloadFromBenchmarkReport(
    const ReachabilityBenchmarkReport& report,
    const ReachabilityBenchmarkConfig& config
) noexcept {
    BenchmarkCampaignQueryWorkload workload{};
    workload.pair_seed = config.pair_seed;
    workload.query_count = config.query_count;
    workload.warmup_queries = config.warmup_queries;
    workload.correctness_check_count = config.correctness_check_count;
    workload.pair_list_hash = report.pair_list_hash;
    return workload;
}

std::vector<BenchmarkCampaignResultRow> benchmarkCampaignRowsFromReport(
    const BenchmarkCampaignMapContext& map,
    const BenchmarkCampaignMetadata& metadata,
    const BenchmarkCampaignQueryWorkload& workload,
    const ReachabilityBenchmarkReport& report,
    const uint64_t peak_preprocess_rss_bytes
) {
    std::vector<BenchmarkCampaignResultRow> rows;
    rows.reserve(report.methods.size());
    const std::string run_timestamp = currentUtcTimestamp();
    for (const BaselineBenchmarkMetrics& method_metrics : report.methods) {
        BenchmarkCampaignResultRow row{};
        row.map = map;
        row.metrics = method_metrics;
        row.workload = workload;
        row.campaign_id = metadata.campaign_id;
        row.run_timestamp_utc = run_timestamp;
        row.num_vertices = report.num_vertices;
        row.num_edges = report.num_edges;
        row.peak_preprocess_rss_bytes = peak_preprocess_rss_bytes;
        rows.push_back(row);
    }
    return rows;
}

bool writeBenchmarkCampaignWorkloadJson(
    const std::filesystem::path& workload_json,
    const BenchmarkCampaignQueryWorkload& workload,
    std::string& error_message
) {
    std::ostringstream stream;
    stream << "{\n"
           << "  \"schema_version\": \"" << kBenchmarkCampaignSchemaVersion << "\",\n"
           << "  \"pair_seed\": " << workload.pair_seed << ",\n"
           << "  \"query_count\": " << workload.query_count << ",\n"
           << "  \"warmup_queries\": " << workload.warmup_queries << ",\n"
           << "  \"correctness_check_count\": " << workload.correctness_check_count
           << ",\n"
           << "  \"pair_list_hash\": " << workload.pair_list_hash << "\n"
           << "}\n";
    return writeTextFile(workload_json, stream.str(), error_message);
}

bool appendBenchmarkCampaignManifestCsv(
    const std::filesystem::path& manifest_csv,
    const BenchmarkCampaignMetadata& metadata,
    const BenchmarkCampaignMapContext& map,
    const uint32_t num_vertices,
    const uint64_t num_edges,
    std::string& error_message
) {
    const bool needs_header = !fileExists(manifest_csv);
    std::ofstream output(manifest_csv, std::ios::out | std::ios::app);
    if (!output.is_open()) {
        error_message = "Failed to open manifest.csv for append";
        return false;
    }
    if (needs_header) {
        output << campaignManifestCsvHeader();
    }
    output << kBenchmarkCampaignSchemaVersion << ','
           << csvEscape(metadata.campaign_id) << ','
           << csvEscape(map.map_id) << ','
           << csvEscape(map.generator_type) << ','
           << csvEscape(map.recipe_path) << ','
           << csvEscape(map.gallery_image_path) << ','
           << num_vertices << ','
           << num_edges << '\n';
    if (!output.good()) {
        error_message = "Failed to append manifest.csv row";
        return false;
    }
    return true;
}

bool appendBenchmarkCampaignResultsCsv(
    const std::filesystem::path& results_csv,
    const std::span<const BenchmarkCampaignResultRow> rows,
    std::string& error_message
) {
    if (rows.empty()) {
        return true;
    }

    const bool needs_header = !fileExists(results_csv);
    std::ofstream output(results_csv, std::ios::out | std::ios::app);
    if (!output.is_open()) {
        error_message = "Failed to open results.csv for append";
        return false;
    }
    if (needs_header) {
        output << campaignResultsCsvHeader();
    }
    for (const BenchmarkCampaignResultRow& row : rows) {
        output << formatResultCsvRow(row);
    }
    if (!output.good()) {
        error_message = "Failed to append results.csv rows";
        return false;
    }
    return true;
}

bool writeBenchmarkCampaignSummaryMd(
    const std::filesystem::path& summary_md,
    const BenchmarkCampaignMetadata& metadata,
    const std::span<const BenchmarkCampaignResultRow> rows,
    std::string& error_message
) {
    std::ostringstream stream;
    stream << "# Benchmark campaign summary\n\n"
           << "| Field | Value |\n"
           << "|-------|-------|\n"
           << "| Campaign | `" << metadata.campaign_id << "` |\n"
           << "| Schema | `" << metadata.schema_version << "` |\n"
           << "| Started | " << metadata.started_at_utc << " |\n"
           << "| Git | `" << metadata.git_commit << "` |\n"
           << "| Build | " << metadata.build_type << " |\n"
           << "| Compiler | " << metadata.compiler_id << " "
           << metadata.compiler_version << " |\n"
           << "| CPU | " << metadata.cpu_model << " |\n"
           << "| Threads | " << metadata.hardware_concurrency << " |\n\n"
           << "## Results\n\n"
           << "| Map | Method | Status | Preprocess | QPS | Total vs BFS |\n"
           << "|-----|--------|--------|------------|-----|-------------|\n";

    for (const BenchmarkCampaignResultRow& row : rows) {
        char preprocess_buffer[32];
        char speedup_buffer[32];
        formatBenchmarkNanoseconds(
            preprocess_buffer,
            sizeof(preprocess_buffer),
            static_cast<double>(row.metrics.preprocess_nanoseconds)
        );
        formatBenchmarkSpeedupRatio(
            speedup_buffer,
            sizeof(speedup_buffer),
            row.metrics.total_speedup_vs_bfs
        );
        stream << "| " << row.map.map_id << " | "
               << reachabilityBaselineName(row.metrics.method) << " | "
               << baselineStatusLabel(row.metrics.status) << " | "
               << preprocess_buffer << " | "
               << row.metrics.query_stats.queries_per_second << " | "
               << speedup_buffer << " |\n";
    }

    return writeTextFile(summary_md, stream.str(), error_message);
}

const char* baselineStatusLabel(const BaselineStatus status) noexcept {
    switch (status) {
        case BaselineStatus::NotRun:
            return "NotRun";
        case BaselineStatus::Completed:
            return "Completed";
        case BaselineStatus::SkippedByPolicy:
            return "SkippedByPolicy";
        case BaselineStatus::OutOfMemory:
            return "OutOfMemory";
        case BaselineStatus::Failed:
            return "Failed";
    }
    return "Unknown";
}

bool runBenchmarkCampaignGridJob(
    const BenchmarkCampaignPaths& paths,
    const BenchmarkCampaignMetadata& metadata,
    const BenchmarkCampaignMapContext& map,
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    ReachabilityBenchmarkConfig config,
    std::string& error_message
) {
    std::vector<uint32_t> query_universe(graph.numVertices());
    for (uint32_t vertex = 0U; vertex < graph.numVertices(); ++vertex) {
        query_universe[vertex] = vertex;
    }

    const uint64_t rss_before = currentProcessRssBytes();
    const ReachabilityBenchmarkReport report =
        ReachabilityBenchmarkJob::run(graph, layout, query_universe, config);
    const uint64_t rss_after = currentProcessRssBytes();
    const uint64_t peak_rss = rss_after > rss_before ? rss_after : rss_before;

    if (!report.valid) {
        error_message = "ReachabilityBenchmarkJob returned an invalid report";
        return false;
    }

    if (!appendBenchmarkCampaignManifestCsv(
            paths.manifest_csv,
            metadata,
            map,
            report.num_vertices,
            report.num_edges,
            error_message)) {
        return false;
    }

    const BenchmarkCampaignQueryWorkload workload =
        workloadFromBenchmarkReport(report, config);
    if (!writeBenchmarkCampaignWorkloadJson(paths.workload_json, workload, error_message)) {
        return false;
    }

    const std::vector<BenchmarkCampaignResultRow> rows =
        benchmarkCampaignRowsFromReport(map, metadata, workload, report, peak_rss);
    if (!appendBenchmarkCampaignResultsCsv(paths.results_csv, rows, error_message)) {
        return false;
    }
    return writeBenchmarkCampaignSummaryMd(
        paths.summary_md,
        metadata,
        rows,
        error_message
    );
}

}  // namespace hbrick
