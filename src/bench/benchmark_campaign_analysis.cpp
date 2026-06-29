#include "hbrick/bench/benchmark_campaign_analysis.hpp"

#include <fstream>
#include <sstream>
#include <unordered_map>

#include "hbrick/bench/benchmark_campaign_config.hpp"
#include "hbrick/bench/reachability_benchmark_format.hpp"

namespace hbrick {

namespace {

[[nodiscard]] bool splitCsvLine(
    const std::string& line,
    std::vector<std::string>& fields
) {
    fields.clear();
    std::string current;
    bool in_quotes = false;
    for (std::size_t index = 0U; index < line.size(); ++index) {
        const char character = line[index];
        if (in_quotes) {
            if (character == '"') {
                if (index + 1U < line.size() && line[index + 1U] == '"') {
                    current.push_back('"');
                    ++index;
                } else {
                    in_quotes = false;
                }
            } else {
                current.push_back(character);
            }
            continue;
        }
        if (character == '"') {
            in_quotes = true;
            continue;
        }
        if (character == ',') {
            fields.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(character);
    }
    fields.push_back(current);
    return !in_quotes;
}

struct ParsedResultRow {
    std::string map_id;
    std::string map_class;
    std::string method;
    std::string status;
    std::string config_id;
    double queries_per_second = 0.0;
    double total_speedup_vs_bfs = 0.0;
    uint32_t correctness_mismatches = 0U;
};

[[nodiscard]] bool parseResultRow(
    const std::vector<std::string>& fields,
    ParsedResultRow& row
) {
    if (fields.size() < 25U) {
        return false;
    }
    row.map_id = fields[3];
    row.method = fields[5];
    row.status = fields[6];
    row.queries_per_second = std::stod(fields[24]);
    row.total_speedup_vs_bfs = std::stod(fields[27]);
    row.correctness_mismatches = static_cast<uint32_t>(std::stoul(fields[29]));
    if (fields.size() > 34U) {
        row.config_id = fields[34];
    }
    if (fields.size() > 42U) {
        row.map_class = fields[42];
    }
    return true;
}

}  // namespace

std::string benchmarkCampaignMapClass(
    const std::string& generator_type,
    const BenchmarkCampaignMapCharacterization& characterization
) noexcept {
    if (generator_type == "movingai") {
        return "movingai";
    }
    if (generator_type == "procedural_maze") {
        if (characterization.extra_openings == 0U) {
            return "perfect_maze";
        }
        return "cyclic_maze";
    }
    if (generator_type == "smoke_grid") {
        return "smoke_grid";
    }
    if (!characterization.orientation_mode.empty()) {
        return characterization.orientation_mode;
    }
    return generator_type.empty() ? "unknown" : generator_type;
}

const char* benchmarkCampaignResultsCsvHeaderLine() noexcept {
    static const char kHeader[] =
        "campaign_schema_version,campaign_id,run_timestamp_utc,map_id,generator_type,"
        "method,status,policy_skip_detail,num_vertices,num_edges,pair_seed,pair_list_hash,"
        "query_count,warmup_queries,correctness_check_count,preprocess_ns,"
        "estimated_index_bytes,peak_preprocess_rss_bytes,query_count_timed,"
        "query_mean_ns,query_median_ns,query_p95_ns,query_min_ns,query_max_ns,"
        "queries_per_second,total_benchmark_ns,speedup_vs_bfs,total_speedup_vs_bfs,"
        "correctness_checks,correctness_mismatches,correctness_failed,warshall_matrix_order,"
        "kleene_parallel,kleene_thread_count,config_id,brick_tile_width,"
        "brick_tile_height,max_memory_bytes,hbrick_group_width,hbrick_group_height,"
        "hbrick_max_depth,closure_early_stop,map_class,passable_cells,num_sccs,"
        "kleene_rounds_scheduled,kleene_rounds_effective,timer_source\n";
    return kHeader;
}

bool regenerateBenchmarkCampaignSummaryFromResults(
    const std::filesystem::path& results_csv,
    const std::filesystem::path& summary_md,
    const BenchmarkCampaignMetadata& metadata,
    std::string& error_message
) {
    std::ifstream input(results_csv);
    if (!input.is_open()) {
        error_message = "Failed to open results.csv for summary";
        return false;
    }

    std::string line;
    if (!std::getline(input, line)) {
        error_message = "results.csv is empty";
        return false;
    }

    std::vector<std::string> fields;
    std::vector<ParsedResultRow> rows;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        if (!splitCsvLine(line, fields)) {
            error_message = "Malformed results.csv row";
            return false;
        }
        ParsedResultRow row{};
        if (!parseResultRow(fields, row)) {
            continue;
        }
        rows.push_back(row);
    }

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
           << "| Threads | " << metadata.hardware_concurrency << " |\n"
           << "| Timer | `std::chrono::steady_clock` |\n\n"
           << "## Results\n\n"
           << "| Map | Class | Method | Config | Status | QPS | Total vs BFS | Mismatches |\n"
           << "|-----|-------|--------|--------|--------|-----|-------------|------------|\n";

    for (const ParsedResultRow& row : rows) {
        char speedup_buffer[32];
        formatBenchmarkSpeedupRatio(
            speedup_buffer,
            sizeof(speedup_buffer),
            row.total_speedup_vs_bfs
        );
        stream << "| " << row.map_id << " | " << row.map_class << " | " << row.method
               << " | " << row.config_id << " | " << row.status << " | "
               << row.queries_per_second << " | " << speedup_buffer << " | "
               << row.correctness_mismatches << " |\n";
    }

    std::unordered_map<std::string, const ParsedResultRow*> best_by_class;
    for (const ParsedResultRow& row : rows) {
        if (row.status != "Completed" || row.correctness_mismatches != 0U) {
            continue;
        }
        const std::string key = row.map_class.empty() ? "unknown" : row.map_class;
        const auto iterator = best_by_class.find(key);
        if (iterator == best_by_class.end()
            || row.queries_per_second > iterator->second->queries_per_second) {
            best_by_class[key] = &row;
        }
    }

    if (!best_by_class.empty()) {
        stream << "\n## Best completed QPS by map class\n\n"
               << "| Class | Map | Method | Config | QPS |\n"
               << "|-------|-----|--------|--------|-----|\n";
        for (const auto& [map_class, row] : best_by_class) {
            stream << "| " << map_class << " | " << row->map_id << " | " << row->method
                   << " | " << row->config_id << " | " << row->queries_per_second << " |\n";
        }
    }

    std::ofstream output(summary_md, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        error_message = "Failed to write summary.md";
        return false;
    }
    output << stream.str();
    return output.good();
}

}  // namespace hbrick
