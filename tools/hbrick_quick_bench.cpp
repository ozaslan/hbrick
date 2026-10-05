/**
 * @file hbrick_quick_bench.cpp
 * @brief Standalone single/batch benchmark runner for any recipe and method configuration.
 */

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

#include "hbrick/bench/reachability_benchmark.hpp"
#include "hbrick/bench/benchmark_campaign.hpp"
#include "hbrick/bench/benchmark_campaign_config.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/io/movingai_loader.hpp"
#include "hbrick/io/recipe.hpp"

namespace {

struct RunnerArgs {
    std::filesystem::path recipe_path;
    std::string method_name = "HBrick";
    uint32_t b = 8;
    uint32_t g = 2;
    uint32_t query_count = 4096;
    uint32_t warmup_queries = 64;
    uint32_t correctness_check_count = 256;
    uint64_t pair_seed = 0xBEEFCAFEULL;
    double memory_gib = 8.0;
    uint32_t baseline_timeout_sec = 0;
    uint32_t closure_memory_safety_percent = 110;
    uint32_t scc_closure_max_components = 50000;
    bool kleene_allow_scc_compression = true;
    std::filesystem::path datasets_root = "datasets/movingai";
    std::filesystem::path csv_out = "";
    bool json_output = true;
};

void printHelp(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "  --recipe <path>          Path to recipe JSON file (required)\n"
              << "  --method <name>          Baseline method (default: HBrick)\n"
              << "  --b <size>               Base tile size b (default: 8)\n"
              << "  --g <size>               Hierarchy branching factor g (default: 2)\n"
              << "  --query-count <N>        Timed query count (default: 4096)\n"
              << "  --warmup-queries <N>     Warmup query count (default: 64)\n"
              << "  --correctness-checks <N> Correctness queries (default: 256)\n"
              << "  --pair-seed <S>          Workload RNG seed (default: 0xBEEFCAFE)\n"
              << "  --memory-gib <G>         Memory limit in GiB (default: 8.0)\n"
              << "  --baseline-timeout-sec <S> Preprocess deadline in seconds, 0 disables (default: 0)\n"
              << "  --closure-memory-safety-percent <P> Working-set margin percent (default: 110)\n"
              << "  --scc-closure-max-components <C> Skip SCC-DAG closure when C >= C (default: 50000, 0 disables)\n"
              << "  --flat-no-scc            Flat BRICK closure via direct squaring (hierarchy ablation)\n"
              << "  --datasets-root <path>   MovingAI datasets root (default: datasets/movingai)\n"
              << "  --csv-out <path>         Append result row to CSV file\n"
              << "  --help                   Show this help\n";
}

bool parseArgs(int argc, char** argv, RunnerArgs& args) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printHelp(argv[0]);
            std::exit(0);
        } else if (arg == "--recipe" && i + 1 < argc) {
            args.recipe_path = argv[++i];
        } else if (arg == "--method" && i + 1 < argc) {
            args.method_name = argv[++i];
        } else if (arg == "--b" && i + 1 < argc) {
            args.b = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--g" && i + 1 < argc) {
            args.g = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--query-count" && i + 1 < argc) {
            args.query_count = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--warmup-queries" && i + 1 < argc) {
            args.warmup_queries = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--correctness-checks" && i + 1 < argc) {
            args.correctness_check_count = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--pair-seed" && i + 1 < argc) {
            args.pair_seed = std::stoull(argv[++i]);
        } else if (arg == "--memory-gib" && i + 1 < argc) {
            args.memory_gib = std::stod(argv[++i]);
        } else if (arg == "--baseline-timeout-sec" && i + 1 < argc) {
            args.baseline_timeout_sec = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--closure-memory-safety-percent" && i + 1 < argc) {
            args.closure_memory_safety_percent =
                static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--scc-closure-max-components" && i + 1 < argc) {
            args.scc_closure_max_components =
                static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--flat-no-scc") {
            args.kleene_allow_scc_compression = false;
        } else if (arg == "--datasets-root" && i + 1 < argc) {
            args.datasets_root = argv[++i];
        } else if (arg == "--csv-out" && i + 1 < argc) {
            args.csv_out = argv[++i];
        }
    }
    if (args.recipe_path.empty()) {
        std::cerr << "Error: --recipe is required\n";
        return false;
    }
    return true;
}

std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

} // namespace

int main(int argc, char** argv) {
    RunnerArgs args;
    if (!parseArgs(argc, argv, args)) {
        return 1;
    }

    // 1. Load Recipe
    std::optional<hbrick::tools::Recipe> recipe_opt =
        hbrick::tools::loadRecipe(args.recipe_path);
    if (!recipe_opt.has_value()) {
        std::cerr << "Error: Failed to parse recipe: " << args.recipe_path << "\n";
        return 1;
    }
    const auto& recipe = *recipe_opt;

    // 2. Load Map
    std::filesystem::path map_path =
        args.datasets_root / recipe.set_name / "maps" / recipe.map_name;
    if (!std::filesystem::exists(map_path)) {
        // Fallback search
        map_path = args.datasets_root / recipe.map_name;
    }
    if (!std::filesystem::exists(map_path)) {
        std::cerr << "Error: Map file not found: " << map_path << "\n";
        return 1;
    }

    auto load_result = hbrick::loadMovingAiMap(map_path);
    if (!load_result.ok()) {
        std::cerr << "Error: Failed to load map " << map_path << ": " << load_result.error << "\n";
        return 1;
    }

    const hbrick::MazeLayout layout =
        load_result.map.toMazeLayout(recipe.policy);

    // 3. Build Graph
    hbrick::RandomAsymmetricParams orient_params{};
    orient_params.seed = recipe.seed;
    orient_params.p_bidirectional = static_cast<long double>(recipe.p_bidirectional);
    orient_params.p_one_way = static_cast<long double>(recipe.p_one_way);
    orient_params.p_edge_drop = 0.0L;
    orient_params.gradient_angle_degrees = static_cast<double>(recipe.gradient_angle_degrees);
    orient_params.p_against_gradient = static_cast<long double>(recipe.p_against_gradient);

    std::cerr << "PROGRESS\tphase=build_graph\tpct=10\tdetail=Building directed graph...\n";

    const hbrick::DirectedGridGraph graph =
        hbrick::DirectedGridGraphBuilder::build(
            layout,
            recipe.mode,
            orient_params
        );

    // 4. Setup Benchmark Config
    hbrick::ReachabilityBenchmarkConfig config;
    config.query_count = args.query_count;
    config.warmup_queries = args.warmup_queries;
    config.correctness_check_count = args.correctness_check_count;
    config.pair_seed = args.pair_seed;
    config.brick_tile_size = hbrick::TileSize{args.b, args.b};
    config.hbrick_group_size = hbrick::GroupSize{args.g, args.g};
    config.hbrick_max_depth = hbrick::kHBrickFullDepth;
    config.max_memory_bytes = static_cast<uint64_t>(args.memory_gib * 1024.0 * 1024.0 * 1024.0);
    config.max_preprocess_seconds = args.baseline_timeout_sec;
    config.closure_memory_safety_percent = args.closure_memory_safety_percent;
    config.scc_closure_max_components = args.scc_closure_max_components;
    config.kleene_allow_scc_compression = args.kleene_allow_scc_compression;

    // Parse target method
    std::string method_err;
    std::vector<hbrick::ReachabilityBaselineId> parsed_methods;
    std::vector<std::string> method_names = {args.method_name};
    if (!hbrick::parseReachabilityBaselineNames(method_names, parsed_methods, method_err)) {
        std::cerr << "Error: Unknown method name '" << args.method_name << "': " << method_err << "\n";
        return 1;
    }
    const auto target_method = parsed_methods[0];

    // Always include CsrBfs if the target method is not CsrBfs, so speedup_vs_bfs is computed
    if (target_method == hbrick::ReachabilityBaselineId::CsrBfs) {
        config.methods = { target_method };
    } else {
        config.methods = { hbrick::ReachabilityBaselineId::CsrBfs, target_method };
    }

    // Passable vertices universe
    const uint32_t total_v = layout.width() * layout.height();
    std::vector<uint32_t> universe;
    universe.reserve(total_v);
    for (uint32_t v = 0; v < total_v; ++v) {
        const uint32_t x = v % layout.width();
        const uint32_t y = v / layout.width();
        if (layout.isPassable(x, y)) {
            universe.push_back(v);
        }
    }

    // 5. Run Benchmark
    std::cerr << "PROGRESS\tphase=benchmark_init\tpct=20\tdetail=Initializing benchmark job...\n";
    hbrick::ReachabilityBenchmarkJob job;
    job.begin(graph, layout, universe, config);

    auto last_progress_emit = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto last_progress_stage = hbrick::ReachabilityBenchmarkProgress::Stage::Finished;
    auto last_progress_method = hbrick::ReachabilityBaselineId::CsrBfs;
    bool have_progress_sample = false;

    while (job.active()) {
        (void)job.step();
        const auto prog = job.progress();
        float pct = 20.0f;
        if (prog.work_total > 0) {
            pct = 20.0f + 75.0f * (static_cast<float>(prog.work_completed) / static_cast<float>(prog.work_total));
        }
        const auto now = std::chrono::steady_clock::now();
        const bool stage_changed = !have_progress_sample
            || prog.stage != last_progress_stage
            || prog.current_method != last_progress_method;
        const bool interval_elapsed = now - last_progress_emit >= std::chrono::milliseconds(250);
        if (stage_changed || interval_elapsed || !job.active()) {
            std::string stage_str = "Running";
            if (prog.stage == hbrick::ReachabilityBenchmarkProgress::Stage::GeneratingPairs) stage_str = "Generating Pairs";
            else if (prog.stage == hbrick::ReachabilityBenchmarkProgress::Stage::Preprocessing) stage_str = "Preprocessing";
            else if (prog.stage == hbrick::ReachabilityBenchmarkProgress::Stage::WarmingUp) stage_str = "Warmup";
            else if (prog.stage == hbrick::ReachabilityBenchmarkProgress::Stage::Querying) stage_str = "Timed Queries";
            else if (prog.stage == hbrick::ReachabilityBenchmarkProgress::Stage::CorrectnessCheck) stage_str = "Verifying Correctness";

            std::cerr << "PROGRESS\tphase=" << stage_str
                      << "\tpct=" << std::fixed << std::setprecision(1) << pct
                      << "\tmethod=" << hbrick::reachabilityBaselineName(prog.current_method)
                      << "\twork=" << prog.work_completed << "/" << prog.work_total << "\n";
            last_progress_emit = now;
            last_progress_stage = prog.stage;
            last_progress_method = prog.current_method;
            have_progress_sample = true;
        }
    }

    const auto& report = job.report();
    if (!report.valid) {
        std::cerr << "Error: Benchmark job failed or was aborted.\n";
        return 1;
    }

    // Extract metrics for the target method
    const hbrick::BaselineBenchmarkMetrics* target_metrics = nullptr;
    for (const auto& m : report.methods) {
        if (m.method == target_method) {
            target_metrics = &m;
            break;
        }
    }

    if (!target_metrics) {
        std::cerr << "Error: Target method metrics not found in report.\n";
        return 1;
    }

    const double prep_ms = static_cast<double>(target_metrics->preprocess_nanoseconds) / 1e6;
    const double index_mb = static_cast<double>(target_metrics->measured_retained_index_bytes) / (1024.0 * 1024.0);
    const double scratch_mb = static_cast<double>(target_metrics->scratch_bytes) / (1024.0 * 1024.0);
    const double preprocess_rss_delta_mb = static_cast<double>(target_metrics->preprocess_rss_delta_bytes) / (1024.0 * 1024.0);
    const double query_mean_ns = target_metrics->query_stats.mean_nanoseconds;
    const double qps = target_metrics->query_stats.queries_per_second;
    const double speedup = target_metrics->speedup_vs_bfs;
    const bool passed = (target_metrics->correctness_mismatches == 0 && target_metrics->status == hbrick::BaselineStatus::Completed);
    const std::string status_str = hbrick::baselineStatusLabel(target_metrics->status);

    const bool is_tile_method =
        (args.method_name.find("Brick") != std::string::npos ||
         args.method_name.find("brick") != std::string::npos);
    const bool is_hbrick =
        (args.method_name.find("HBrick") != std::string::npos ||
         args.method_name.find("hbrick") != std::string::npos);
    const bool uses_b = is_tile_method;
    const bool uses_g = is_hbrick;
    const std::string b_str = uses_b ? std::to_string(args.b) : "-";
    const std::string g_str = uses_g ? std::to_string(args.g) : "-";

    std::cerr << "PROGRESS\tphase=completed\tpct=100\tdetail=Done!\n";

    // 6. JSON Output to stdout
    std::cout << "{\n"
              << "  \"recipe_label\": \"" << recipe.label << "\",\n"
              << "  \"recipe_file\": \"" << args.recipe_path.filename().string() << "\",\n"
              << "  \"map_name\": \"" << recipe.map_name << "\",\n"
              << "  \"set_name\": \"" << recipe.set_name << "\",\n"
              << "  \"method\": \"" << args.method_name << "\",\n"
              << "  \"status\": \"" << status_str << "\",\n"
              << "  \"is_tile\": " << (is_tile_method ? "true" : "false") << ",\n"
              << "  \"b\": " << (uses_b ? std::to_string(args.b) : "null") << ",\n"
              << "  \"g\": " << (uses_g ? std::to_string(args.g) : "null") << ",\n"
              << "  \"vertices\": " << report.num_vertices << ",\n"
              << "  \"edges\": " << report.num_edges << ",\n"
              << "  \"query_count\": " << report.query_pair_count << ",\n"
              << "  \"preprocess_ms\": " << std::fixed << std::setprecision(2) << prep_ms << ",\n"
              << "  \"index_mb\": " << std::setprecision(3) << index_mb << ",\n"
              << "  \"scratch_mb\": " << std::setprecision(3) << scratch_mb << ",\n"
              << "  \"preprocess_rss_delta_mb\": " << std::setprecision(3) << preprocess_rss_delta_mb << ",\n"
              << "  \"query_mean_ns\": " << std::setprecision(1) << query_mean_ns << ",\n"
              << "  \"qps\": " << std::setprecision(1) << qps << ",\n"
              << "  \"speedup_vs_bfs\": " << std::setprecision(2) << speedup << ",\n"
              << "  \"correctness_checks\": " << target_metrics->correctness_checks << ",\n"
              << "  \"correctness_mismatches\": " << target_metrics->correctness_mismatches << ",\n"
              << "  \"estimated_index_bytes\": " << target_metrics->estimated_index_bytes << ",\n"
              << "  \"scc_components\": " << target_metrics->scc_components << ",\n"
              << "  \"scc_condensation_edges\": " << target_metrics->scc_condensation_edges << ",\n"
              << "  \"skip_detail\": \"" << target_metrics->policy_skip_detail << "\",\n"
              << "  \"warshall_pivot_total\": " << target_metrics->warshall_pivot_total << ",\n"
              << "  \"warshall_pivots_completed\": " << target_metrics->warshall_pivots_completed << ",\n"
              << "  \"warmup_queries\": " << args.warmup_queries << ",\n"
              << "  \"requested_correctness_checks\": " << args.correctness_check_count << ",\n"
              << "  \"pair_seed\": " << args.pair_seed << ",\n"
              << "  \"memory_gib\": " << std::setprecision(3) << args.memory_gib << ",\n"
              << "  \"passed\": " << (passed ? "true" : "false") << "\n"
              << "}\n";

    // 7. Append to CSV if requested
    if (!args.csv_out.empty()) {
        const bool write_header = !std::filesystem::exists(args.csv_out) || std::filesystem::file_size(args.csv_out) == 0;
        std::ofstream csv(args.csv_out, std::ios::app);
        if (csv.is_open()) {
            if (write_header) {
                csv << "timestamp,recipe_label,map_name,set_name,method,status,b,g,vertices,edges,preprocess_ms,index_mb,query_mean_ns,qps,speedup_vs_bfs,passed,error\n";
            }
            csv << currentTimestamp() << ','
                << recipe.label << ','
                << recipe.map_name << ','
                << recipe.set_name << ','
                << args.method_name << ','
                << status_str << ','
                << b_str << ','
                << g_str << ','
                << report.num_vertices << ','
                << report.num_edges << ','
                << std::fixed << std::setprecision(2) << prep_ms << ','
                << std::setprecision(3) << index_mb << ','
                << std::setprecision(1) << query_mean_ns << ','
                << std::setprecision(1) << qps << ','
                << std::setprecision(2) << speedup << ','
                << (passed ? "true" : "false") << ",\n";
        }
    }

    return 0;
}
