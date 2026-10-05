#include "hbrick/bench/benchmark_campaign.hpp"

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdint>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/bench/balanced_query_workload.hpp"
#include "hbrick/bench/benchmark_campaign_log.hpp"
#include "hbrick/bench/benchmark_campaign_analysis.hpp"
#include "hbrick/bench/benchmark_campaign_config.hpp"
#include "hbrick/bench/process_memory.hpp"
#include "hbrick/bench/reachability_benchmark_format.hpp"
#include "hbrick/bench/reachability_benchmark_util.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/passable_vertex_map.hpp"

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

[[nodiscard]] bool fileIsEmpty(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return !error && size == 0U;
}

[[nodiscard]] std::string campaignResultsCsvHeader() {
    return benchmarkCampaignResultsCsvHeaderLine();
}

[[nodiscard]] std::string campaignManifestCsvHeader() {
    return "campaign_schema_version,campaign_id,map_id,generator_type,recipe_path,"
           "gallery_image_path,grid_width,grid_height,passable_cells,num_vertices,"
           "num_edges,undirected_component_count,largest_undirected_component,"
           "num_sccs,largest_scc,condensation_vertices,condensation_edges,"
           "orientation_mode,carve_seed,opening_seed,extra_openings,p_one_way,"
           "p_bidirectional,gradient_angle_degrees,p_against_gradient,"
           "orientation_seed,gallery_image_hash,movingai_set,movingai_map,"
           "datasets_root,passability_policy\n";
}

[[nodiscard]] std::string formatManifestCsvRow(
    const BenchmarkCampaignMetadata& metadata,
    const BenchmarkCampaignMapContext& map,
    const BenchmarkCampaignMapCharacterization& characterization
) {
    std::ostringstream stream;
    stream << kBenchmarkCampaignSchemaVersion << ','
           << csvEscape(metadata.campaign_id) << ','
           << csvEscape(map.map_id) << ','
           << csvEscape(map.generator_type) << ','
           << csvEscape(map.recipe_path) << ','
           << csvEscape(map.gallery_image_path) << ','
           << characterization.grid_width << ','
           << characterization.grid_height << ','
           << characterization.passable_cells << ','
           << characterization.num_vertices << ','
           << characterization.num_edges << ','
           << characterization.undirected_component_count << ','
           << characterization.largest_undirected_component << ','
           << characterization.num_strongly_connected_components << ','
           << characterization.largest_strongly_connected_component << ','
           << characterization.condensation_vertices << ','
           << characterization.condensation_edges << ','
           << csvEscape(characterization.orientation_mode) << ','
           << characterization.carve_seed << ','
           << characterization.opening_seed << ','
           << characterization.extra_openings << ','
           << characterization.p_one_way << ','
           << characterization.p_bidirectional << ','
           << characterization.gradient_angle_degrees << ','
           << characterization.p_against_gradient << ','
           << characterization.orientation_seed << ','
           << characterization.gallery_image_hash << ','
           << csvEscape(characterization.movingai_set) << ','
           << csvEscape(characterization.movingai_map) << ','
           << csvEscape(characterization.datasets_root) << ','
           << csvEscape(characterization.passability_policy) << '\n';
    return stream.str();
}

[[nodiscard]] std::string formatResultCsvRow(const BenchmarkCampaignResultRow& row) {
    const bool correctness_failed = row.metrics.correctness_mismatches > 0U;
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
           << row.metrics.measured_retained_index_bytes << ','
           << row.peak_preprocess_rss_bytes << ','
           << row.metrics.query_stats.count << ','
           << row.metrics.query_stats.mean_nanoseconds << ','
           << row.metrics.query_stats.median_nanoseconds << ','
           << row.metrics.query_stats.p95_nanoseconds << ','
           << row.metrics.query_stats.min_nanoseconds << ','
           << row.metrics.query_stats.max_nanoseconds << ','
           << row.metrics.query_stats.queries_per_second << ','
           << row.metrics.total_benchmark_nanoseconds << ','
           << row.metrics.speedup_vs_bfs << ','
           << row.metrics.total_speedup_vs_bfs << ','
           << row.metrics.correctness_checks << ','
           << row.metrics.correctness_mismatches << ','
           << (correctness_failed ? "true" : "false") << ','
           << row.metrics.warshall_matrix_order << ','
           << (row.metrics.kleene_parallel ? "true" : "false") << ','
           << row.metrics.kleene_thread_count << ','
           << csvEscape(row.run_parameters.config_id) << ','
           << row.run_parameters.brick_tile_width << ','
           << row.run_parameters.brick_tile_height << ','
           << row.run_parameters.max_memory_bytes << ','
           << row.run_parameters.hbrick_group_width << ','
           << row.run_parameters.hbrick_group_height << ','
           << row.run_parameters.hbrick_max_depth << ','
           << (row.run_parameters.closure_early_stop ? "true" : "false") << ','
           << csvEscape(row.map_class) << ','
           << row.passable_cells << ','
           << row.num_sccs << ','
           << row.metrics.kleene_rounds_scheduled << ','
           << row.metrics.kleene_rounds_effective << ','
           << "steady_clock" << ','
           << row.metrics.scratch_bytes << ','
           << row.metrics.hbrick_local_hits << ','
           << row.metrics.hbrick_ancestor_hits << ','
           << row.metrics.hbrick_fallback_count << ','
           << row.metrics.hbrick_fallback_rate << ','
           << row.metrics.mean_positive_query_nanoseconds << ','
           << row.metrics.mean_negative_query_nanoseconds << ','
           << csvEscape(row.metrics.hbrick_level_stats) << ','
           << row.metrics.preprocess_rss_delta_bytes << '\n';
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

[[nodiscard]] std::string stripCsvHeaderNewlines(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

/** @brief Schema-v3 results header: v4 with @c aux_index_bytes. */
[[nodiscard]] std::string resultsCsvHeaderV3() {
    std::string header = benchmarkCampaignResultsCsvHeaderLine();
    constexpr std::string_view kFrom = "measured_retained_index_bytes";
    constexpr std::string_view kTo = "aux_index_bytes";
    const auto pos = header.find(kFrom);
    if (pos != std::string::npos) {
        header.replace(pos, kFrom.size(), kTo);
    }
    return header;
}

[[nodiscard]] std::string bumpResultsSchemaFieldIfV3(const std::string& line) {
    if (line.size() >= 2U && line[0] == '3' && line[1] == ',') {
        return std::string("4") + line.substr(1U);
    }
    return line;
}

[[nodiscard]] bool rewriteResultsCsvMigratingV3(
    const std::filesystem::path& results_csv,
    std::string& error_message
) {
    std::ifstream input(results_csv);
    if (!input.is_open()) {
        error_message = "Failed to open results.csv for schema migration";
        return false;
    }
    std::string header;
    if (!std::getline(input, header)) {
        error_message = "Failed to read results.csv header for schema migration";
        return false;
    }

    std::ostringstream body;
    body << campaignResultsCsvHeader();
    std::string line;
    while (std::getline(input, line)) {
        body << bumpResultsSchemaFieldIfV3(line) << '\n';
    }
    input.close();

    const std::filesystem::path tmp =
        results_csv.parent_path() / (results_csv.filename().string() + ".schema.tmp");
    if (!writeTextFile(tmp, body.str(), error_message)) {
        return false;
    }
    std::error_code error;
    std::filesystem::rename(tmp, results_csv, error);
    if (error) {
        std::filesystem::remove(tmp, error);
        error_message = "Failed to replace results.csv after schema migration";
        return false;
    }
    return true;
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
    paths.workloads_dir = campaign_root / "workloads";
    return paths;
}

bool ensureBenchmarkCampaignResultsCsvSchema(
    const std::filesystem::path& results_csv,
    std::string& error_message
) {
    if (!fileExists(results_csv)) {
        return true;
    }

    std::ifstream input(results_csv);
    if (!input.is_open()) {
        error_message = "Failed to open results.csv to check schema";
        return false;
    }
    std::string header;
    if (!std::getline(input, header)) {
        return true;
    }
    input.close();

    const std::string stripped = stripCsvHeaderNewlines(header);
    if (stripped.empty()) {
        return true;
    }
    if (stripped == stripCsvHeaderNewlines(campaignResultsCsvHeader())) {
        return true;
    }
    if (stripped == stripCsvHeaderNewlines(resultsCsvHeaderV3())) {
        return rewriteResultsCsvMigratingV3(results_csv, error_message);
    }

    error_message =
        "results.csv header is incompatible with campaign schema "
        + std::string(kBenchmarkCampaignSchemaVersion)
        + "; start a new campaign or migrate the file";
    return false;
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
         {paths.gallery_dir, paths.recipes_dir, paths.logs_dir, paths.workloads_dir}) {
        if (!std::filesystem::create_directories(directory, error) && error) {
            error_message = "Failed to create directory: " + error.message();
            return false;
        }
    }

    if (metadata.started_at_utc.empty()) {
        metadata.started_at_utc = currentUtcTimestamp();
    }
    // Never clobber an existing campaign: init is create-only for CSV/docs.
    // Re-running prepare / init must keep manifest.csv and results.csv intact.
    if (!fileExists(paths.metadata_json)) {
        if (!writeMetadataJson(paths.metadata_json, metadata, error_message)) {
            return false;
        }
    }
    if (!fileExists(paths.manifest_csv)) {
        if (!writeTextFile(paths.manifest_csv, campaignManifestCsvHeader(), error_message)) {
            return false;
        }
    }
    if (!fileExists(paths.results_csv)) {
        if (!writeTextFile(paths.results_csv, campaignResultsCsvHeader(), error_message)) {
            return false;
        }
    } else if (!ensureBenchmarkCampaignResultsCsvSchema(paths.results_csv, error_message)) {
        return false;
    }

    if (!fileExists(paths.summary_md)) {
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
        if (!writeTextFile(paths.summary_md, summary.str(), error_message)) {
            return false;
        }
    }
    return true;
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
    const ReachabilityBenchmarkConfig& config,
    const uint64_t peak_preprocess_rss_bytes,
    const std::string& map_class,
    const BenchmarkCampaignMapCharacterization* map_characterization
) {
    std::vector<BenchmarkCampaignResultRow> rows;
    rows.reserve(report.methods.size());
    const std::string run_timestamp = currentUtcTimestamp();
    const BenchmarkCampaignRunParameters run_parameters =
        benchmarkCampaignRunParametersFromConfig(config);
    BenchmarkCampaignMapCharacterization empty_characterization{};
    const BenchmarkCampaignMapCharacterization& characterization =
        map_characterization != nullptr ? *map_characterization : empty_characterization;
    const std::string resolved_map_class = map_class.empty()
        ? benchmarkCampaignMapClass(map.generator_type, characterization)
        : map_class;
    for (const BaselineBenchmarkMetrics& method_metrics : report.methods) {
        BenchmarkCampaignResultRow row{};
        row.map = map;
        row.metrics = method_metrics;
        row.workload = workload;
        row.run_parameters = run_parameters;
        row.campaign_id = metadata.campaign_id;
        row.run_timestamp_utc = run_timestamp;
        row.num_vertices = report.num_vertices;
        row.num_edges = report.num_edges;
        row.peak_preprocess_rss_bytes = peak_preprocess_rss_bytes;
        row.map_class = resolved_map_class;
        row.passable_cells = characterization.passable_cells;
        row.num_sccs = characterization.num_strongly_connected_components;
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
           << "  \"pair_list_hash\": " << workload.pair_list_hash << ",\n"
           << "  \"balanced_workload\": "
           << (workload.balanced_workload ? "true" : "false") << ",\n"
           << "  \"balanced_complete\": "
           << (workload.balanced_complete ? "true" : "false") << ",\n"
           << "  \"balanced_requested_per_bucket\": [";
    for (uint32_t bucket = 0U; bucket < 6U; ++bucket) {
        if (bucket > 0U) {
            stream << ", ";
        }
        stream << workload.balanced_requested_per_bucket[bucket];
    }
    stream << "],\n"
           << "  \"balanced_filled_per_bucket\": [";
    for (uint32_t bucket = 0U; bucket < 6U; ++bucket) {
        if (bucket > 0U) {
            stream << ", ";
        }
        stream << workload.balanced_filled_per_bucket[bucket];
    }
    stream << "]\n"
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
    BenchmarkCampaignMapCharacterization characterization{};
    characterization.num_vertices = num_vertices;
    characterization.num_edges = num_edges;
    return appendBenchmarkCampaignManifestRowFull(
        manifest_csv,
        metadata,
        map,
        characterization,
        error_message
    );
}

bool appendBenchmarkCampaignManifestRowFull(
    const std::filesystem::path& manifest_csv,
    const BenchmarkCampaignMetadata& metadata,
    const BenchmarkCampaignMapContext& map,
    const BenchmarkCampaignMapCharacterization& characterization,
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
    output << formatManifestCsvRow(metadata, map, characterization);
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

    if (fileExists(results_csv) && !fileIsEmpty(results_csv)
        && !ensureBenchmarkCampaignResultsCsvSchema(results_csv, error_message)) {
        return false;
    }

    const bool needs_header = !fileExists(results_csv) || fileIsEmpty(results_csv);
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
    output.flush();
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
           << "| Map | Method | Config | Status | Preprocess | QPS | Total vs BFS |\n"
           << "|-----|--------|--------|--------|------------|-----|-------------|\n";

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
               << row.run_parameters.config_id << " | "
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
        case BaselineStatus::Timeout:
            return "Timeout";
    }
    return "Unknown";
}

#if !defined(_WIN32)
enum class ChildWaitOutcome : uint8_t {
    Exited = 0,
    TimedOut,
    WaitError
};

[[nodiscard]] ChildWaitOutcome waitPidWithTimeout(
    const pid_t pid,
    const uint32_t timeout_seconds,
    int& status
) {
    if (timeout_seconds == 0U) {
        if (waitpid(pid, &status, 0) < 0) {
            return ChildWaitOutcome::WaitError;
        }
        return ChildWaitOutcome::Exited;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    while (true) {
        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            return ChildWaitOutcome::Exited;
        }
        if (waited < 0 && errno != EINTR) {
            return ChildWaitOutcome::WaitError;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return ChildWaitOutcome::TimedOut;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
#endif

[[nodiscard]] std::filesystem::path frozenWorkloadCsvPath(
    const BenchmarkCampaignPaths& paths,
    const std::string& map_id,
    const uint64_t seed
) {
    return paths.workloads_dir / (map_id + "_" + std::to_string(seed) + ".csv");
}

[[nodiscard]] std::filesystem::path frozenPolarityWorkloadCsvPath(
    const BenchmarkCampaignPaths& paths,
    const std::string& map_id,
    const uint64_t seed
) {
    return paths.workloads_dir
        / (map_id + "_" + std::to_string(seed) + "_polarity.csv");
}

[[nodiscard]] bool prepareFrozenCampaignPairs(
    const BenchmarkCampaignPaths& paths,
    const BenchmarkCampaignMapContext& map,
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const ReachabilityBenchmarkConfig& config,
    std::vector<ReachabilityQueryPair>& frozen,
    BenchmarkCampaignQueryWorkload& workload_meta,
    std::string& error_message
) {
    frozen.clear();
    workload_meta.balanced_workload = config.use_balanced_workload;
    if (config.use_polarity_split_workload && config.use_balanced_workload) {
        error_message =
            "use_polarity_split_workload and use_balanced_workload cannot both be set";
        return false;
    }
    if (!config.use_balanced_workload && !config.use_polarity_split_workload) {
        return true;
    }

    std::error_code dir_error;
    if (!paths.workloads_dir.empty()) {
        std::filesystem::create_directories(paths.workloads_dir, dir_error);
        if (dir_error) {
            error_message =
                "Failed to create workloads directory: " + dir_error.message();
            return false;
        }
    }

    const std::filesystem::path csv_path = config.use_polarity_split_workload
        ? frozenPolarityWorkloadCsvPath(paths, map.map_id, config.pair_seed)
        : frozenWorkloadCsvPath(paths, map.map_id, config.pair_seed);
    if (config.use_polarity_split_workload) {
        BalancedWorkload csv_workload{};
        if (!csv_path.empty() && fileExists(csv_path)) {
            if (!readBalancedWorkloadCsv(csv_path, csv_workload, error_message)) {
                return false;
            }
        } else {
            const PassableVertexMap passable = PassableVertexMap::fromLayout(layout);
            const CsrGraph compact = inducePassableCsr(graph, passable);
            PolaritySplitWorkloadSpec spec{};
            spec.positive_count = config.query_count;
            spec.negative_count = config.query_count;
            spec.seed = config.pair_seed;
            spec.fail_if_shortfall = true;
            const PolaritySplitWorkload split =
                generatePolaritySplitWorkload(compact, passable, spec);
            if (!split.complete) {
                error_message =
                    "Polarity-split workload shortfall for map " + map.map_id
                    + " (positive "
                    + std::to_string(split.filled_positive) + "/"
                    + std::to_string(split.requested_positive)
                    + ", negative "
                    + std::to_string(split.filled_negative) + "/"
                    + std::to_string(split.requested_negative)
                    + "). The graph may be nearly strongly connected.";
                return false;
            }
            csv_workload.pairs = split.pairs;
            csv_workload.pair_list_hash = split.pair_list_hash;
            if (!csv_path.empty()
                && !writeBalancedWorkloadCsv(csv_path, csv_workload, error_message)) {
                return false;
            }
        }
        frozen = std::move(csv_workload.pairs);
        return true;
    }

    BalancedWorkload workload{};
    if (!csv_path.empty() && fileExists(csv_path)) {
        if (!readBalancedWorkloadCsv(csv_path, workload, error_message)) {
            return false;
        }
    } else {
        const PassableVertexMap passable = PassableVertexMap::fromLayout(layout);
        const CsrGraph compact = inducePassableCsr(graph, passable);
        BalancedWorkloadSpec spec{};
        spec.query_count = config.query_count;
        spec.seed = config.pair_seed;
        spec.fail_if_shortfall = config.require_balanced_workload;
        workload = generateBalancedWorkload(layout, compact, passable, spec);
        if (config.require_balanced_workload && !workload.complete) {
            error_message =
                "Balanced six-bucket workload shortfall for map " + map.map_id;
            return false;
        }
        if (!csv_path.empty()
            && !writeBalancedWorkloadCsv(csv_path, workload, error_message)) {
            return false;
        }
    }

    frozen = std::move(workload.pairs);
    workload_meta.balanced_complete = workload.complete;
    for (uint32_t bucket = 0U; bucket < 6U; ++bucket) {
        workload_meta.balanced_requested_per_bucket[bucket] =
            workload.requested_per_bucket[bucket];
        workload_meta.balanced_filled_per_bucket[bucket] =
            workload.filled_per_bucket[bucket];
    }
    return true;
}

void applyBalancedWorkloadMeta(
    BenchmarkCampaignQueryWorkload& workload,
    const BenchmarkCampaignQueryWorkload& meta
) noexcept {
    workload.balanced_workload = meta.balanced_workload;
    workload.balanced_complete = meta.balanced_complete;
    for (uint32_t bucket = 0U; bucket < 6U; ++bucket) {
        workload.balanced_requested_per_bucket[bucket] =
            meta.balanced_requested_per_bucket[bucket];
        workload.balanced_filled_per_bucket[bucket] =
            meta.balanced_filled_per_bucket[bucket];
    }
}

[[nodiscard]] bool appendTimeoutCampaignRow(
    const BenchmarkCampaignPaths& paths,
    const BenchmarkCampaignMetadata& metadata,
    const BenchmarkCampaignMapContext& map,
    const ReachabilityBenchmarkConfig& config,
    const ReachabilityBaselineId method,
    const BenchmarkCampaignQueryWorkload& workload,
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const std::string& map_class,
    const BenchmarkCampaignMapCharacterization* map_characterization,
    std::string& error_message
) {
    ReachabilityBenchmarkReport report{};
    report.valid = true;
    if (map_characterization != nullptr) {
        report.num_vertices = map_characterization->num_vertices;
        report.num_edges = map_characterization->num_edges;
    } else {
        report.num_vertices = layout.passableCount();
        report.num_edges = graph.numEdges();
    }
    report.pair_seed = config.pair_seed;
    report.pair_list_hash = workload.pair_list_hash;
    report.query_pair_count = workload.query_count;

    BaselineBenchmarkMetrics metrics{};
    metrics.method = method;
    metrics.status = BaselineStatus::Timeout;
    metrics.policy_skip_detail = "Preprocess exceeded max_preprocess_seconds";
    report.methods.push_back(metrics);

    const std::vector<BenchmarkCampaignResultRow> rows = benchmarkCampaignRowsFromReport(
        map,
        metadata,
        workload,
        report,
        config,
        0U,
        map_class,
        map_characterization
    );
    return appendBenchmarkCampaignResultsCsv(paths.results_csv, rows, error_message);
}

[[nodiscard]] bool runCampaignJobInProcess(
    const BenchmarkCampaignPaths& paths,
    const BenchmarkCampaignMetadata& metadata,
    const BenchmarkCampaignMapContext& map,
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    ReachabilityBenchmarkConfig config,
    const std::vector<ReachabilityQueryPair>& frozen,
    const BenchmarkCampaignQueryWorkload& balanced_meta,
    const bool append_manifest,
    BenchmarkCampaignLogger* logger,
    const std::string& map_class,
    const BenchmarkCampaignMapCharacterization* map_characterization,
    const bool measure_proc_rss,
    std::string& error_message
) {
    const std::vector<uint32_t> query_universe = collectPassableGridVertices(layout);

    const uint64_t rss_before =
        measure_proc_rss ? currentProcessRssBytes() : 0U;
    const uint64_t hwm_before =
        measure_proc_rss ? currentProcessHwmBytes() : 0U;

    ReachabilityBenchmarkJob job;
    if (!frozen.empty()) {
        job.begin(graph, layout, frozen, config);
    } else {
        job.begin(graph, layout, query_universe, config);
    }
    const auto job_started = std::chrono::steady_clock::now();
    auto last_progress_emit = job_started - std::chrono::seconds(1);
    uint64_t last_emitted_done = UINT64_MAX;
    auto emit_progress = [&](const bool force) {
        const ReachabilityBenchmarkProgress progress = job.progress();
        uint64_t done = 0U;
        uint64_t total = 0U;
        int known = 0;
        double pct = -1.0;
        if (progress.stage == ReachabilityBenchmarkProgress::Stage::Preprocessing
            && progress.stage_work_total > 0U) {
            done = progress.stage_work_completed;
            total = progress.stage_work_total;
            known = 1;
            pct = 100.0
                * static_cast<double>(done)
                / static_cast<double>(total);
        } else if (progress.work_total > 0U) {
            done = progress.work_completed;
            total = progress.work_total;
            known = 1;
            pct = 100.0
                * static_cast<double>(done)
                / static_cast<double>(total);
        } else if (progress.stage_work_total > 0U) {
            done = progress.stage_work_completed;
            total = progress.stage_work_total;
            known = 1;
            pct = 100.0
                * static_cast<double>(done)
                / static_cast<double>(total);
        }
        if (pct > 100.0) {
            pct = 100.0;
        }

        const auto now = std::chrono::steady_clock::now();
        const bool progressed = done != last_emitted_done;
        if (!force
            && !progressed
            && now - last_progress_emit < std::chrono::milliseconds(100)) {
            return;
        }
        last_progress_emit = now;
        last_emitted_done = done;

        char detail[256];
        formatReachabilityBenchmarkStageDetail(detail, sizeof(detail), progress);

        const double elapsed_s = std::chrono::duration<double>(now - job_started).count();
        std::fprintf(
            stderr,
            "HBRICK_PROGRESS\tpct=%.2f\tknown=%d\tstage=%d\tmethod=%s\t"
            "done=%llu\ttotal=%llu\telapsed_s=%.2f\tdetail=%s\n",
            pct,
            known,
            static_cast<int>(progress.stage),
            reachabilityBaselineName(progress.current_method),
            static_cast<unsigned long long>(done),
            static_cast<unsigned long long>(total),
            elapsed_s,
            detail
        );
        std::fflush(stderr);
    };

    emit_progress(true);
    while (!job.step()) {
        emit_progress(false);
    }
    emit_progress(true);

    ReachabilityBenchmarkReport report = job.report();
    uint64_t peak_rss = 0U;
    if (measure_proc_rss) {
        const uint64_t hwm_after = currentProcessHwmBytes();
        const uint64_t baseline_rss = rss_before > 0U ? rss_before : hwm_before;
        const uint64_t proc_delta =
            hwm_after > baseline_rss ? hwm_after - baseline_rss : 0U;
        peak_rss = proc_delta;
        for (BaselineBenchmarkMetrics& metrics : report.methods) {
            metrics.preprocess_rss_delta_bytes = proc_delta;
        }
    }

    if (!report.valid) {
        error_message = "ReachabilityBenchmarkJob returned an invalid report";
        if (logger != nullptr) {
            logger->error(error_message.c_str());
        }
        return false;
    }

    if (append_manifest) {
        if (!appendBenchmarkCampaignManifestCsv(
                paths.manifest_csv,
                metadata,
                map,
                report.num_vertices,
                report.num_edges,
                error_message)) {
            return false;
        }
    }

    BenchmarkCampaignQueryWorkload workload =
        workloadFromBenchmarkReport(report, config);
    applyBalancedWorkloadMeta(workload, balanced_meta);
    if (!frozen.empty()) {
        workload.pair_list_hash = hashReachabilityQueryPairs(frozen);
        workload.query_count = static_cast<uint32_t>(frozen.size());
    }
    if (!writeBenchmarkCampaignWorkloadJson(paths.workload_json, workload, error_message)) {
        return false;
    }

    const std::vector<BenchmarkCampaignResultRow> rows =
        benchmarkCampaignRowsFromReport(
            map,
            metadata,
            workload,
            report,
            config,
            peak_rss,
            map_class,
            map_characterization
        );
    if (!appendBenchmarkCampaignResultsCsv(paths.results_csv, rows, error_message)) {
        if (logger != nullptr) {
            logger->error(error_message.c_str());
        }
        return false;
    }

    if (logger != nullptr) {
        for (const BenchmarkCampaignResultRow& row : rows) {
            logger->infof(
                "  %s: %s (preprocess %llu ns, QPS %.1f)",
                reachabilityBaselineName(row.metrics.method),
                baselineStatusLabel(row.metrics.status),
                static_cast<unsigned long long>(row.metrics.preprocess_nanoseconds),
                row.metrics.query_stats.queries_per_second
            );
        }
        logger->infof("Finished map %s", map.map_id.c_str());
    }
    return true;
}

bool runBenchmarkCampaignGridJob(
    const BenchmarkCampaignPaths& paths,
    const BenchmarkCampaignMetadata& metadata,
    const BenchmarkCampaignMapContext& map,
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    ReachabilityBenchmarkConfig config,
    std::string& error_message,
    const bool append_manifest,
    BenchmarkCampaignLogger* logger,
    const std::string& map_class,
    const BenchmarkCampaignMapCharacterization* map_characterization
) {
    if (logger != nullptr) {
        logger->infof(
            "Starting benchmark for map %s config %s (%u vertices, %llu edges)",
            map.map_id.c_str(),
            benchmarkCampaignConfigId(config).c_str(),
            graph.numVertices(),
            static_cast<unsigned long long>(graph.numEdges())
        );
    }

    std::vector<ReachabilityQueryPair> frozen;
    BenchmarkCampaignQueryWorkload balanced_meta{};
    if (!prepareFrozenCampaignPairs(
            paths,
            map,
            graph,
            layout,
            config,
            frozen,
            balanced_meta,
            error_message)) {
        return false;
    }

    const bool isolate = config.isolate_methods && !config.methods.empty();
    if (!isolate) {
        if (!runCampaignJobInProcess(
                paths,
                metadata,
                map,
                graph,
                layout,
                config,
                frozen,
                balanced_meta,
                append_manifest,
                logger,
                map_class,
                map_characterization,
                false,
                error_message)) {
            return false;
        }
        return regenerateBenchmarkCampaignSummaryFromResults(
            paths.results_csv,
            paths.summary_md,
            metadata,
            error_message
        );
    }

#if defined(_WIN32)
    error_message = "isolate_methods is not supported on this platform";
    return false;
#else
    bool append_manifest_once = append_manifest;
    const std::vector<ReachabilityBaselineId> methods = config.methods;
    for (const ReachabilityBaselineId method : methods) {
        ReachabilityBenchmarkConfig child_config = config;
        child_config.methods = {method};
        child_config.isolate_methods = false;

        const pid_t pid = fork();
        if (pid < 0) {
            error_message = "fork() failed while isolating benchmark methods";
            return false;
        }
        if (pid == 0) {
            std::string child_error;
            const bool ok = runCampaignJobInProcess(
                paths,
                metadata,
                map,
                graph,
                layout,
                child_config,
                frozen,
                balanced_meta,
                append_manifest_once,
                logger,
                map_class,
                map_characterization,
                true,
                child_error
            );
            _exit(ok ? 0 : 1);
        }

        int status = 0;
        const ChildWaitOutcome wait_outcome =
            waitPidWithTimeout(pid, config.max_preprocess_seconds, status);
        append_manifest_once = false;
        if (wait_outcome == ChildWaitOutcome::TimedOut) {
            BenchmarkCampaignQueryWorkload timeout_workload{};
            timeout_workload.pair_seed = config.pair_seed;
            timeout_workload.query_count = static_cast<uint32_t>(frozen.size());
            timeout_workload.warmup_queries = config.warmup_queries;
            timeout_workload.correctness_check_count = config.correctness_check_count;
            timeout_workload.pair_list_hash = hashReachabilityQueryPairs(frozen);
            applyBalancedWorkloadMeta(timeout_workload, balanced_meta);
            if (!appendTimeoutCampaignRow(
                    paths,
                    metadata,
                    map,
                    child_config,
                    method,
                    timeout_workload,
                    graph,
                    layout,
                    map_class,
                    map_characterization,
                    error_message)) {
                return false;
            }
            continue;
        }
        if (wait_outcome == ChildWaitOutcome::WaitError
            || !WIFEXITED(status)
            || WEXITSTATUS(status) != 0) {
            error_message = "Isolated method process failed: ";
            error_message += reachabilityBaselineName(method);
            return false;
        }
    }

    return regenerateBenchmarkCampaignSummaryFromResults(
        paths.results_csv,
        paths.summary_md,
        metadata,
        error_message
    );
#endif
}

}  // namespace hbrick
