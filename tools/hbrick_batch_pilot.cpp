/**
 * @file hbrick_batch_pilot.cpp
 * @brief Batch-reachability pilot harness: many-source / many-target workloads
 *        for H-BRICK scalar, Batch H-BRICK, O'Reach, BFS, and SCC-DAG methods.
 *
 * One invocation covers one frozen recipe (map + orientation) and a list of
 * batch sizes k. Endpoint sets are sampled deterministically and recorded in the
 * JSON output. Every method's answers are checked against a BFS oracle that runs
 * ONE traversal per distinct source, which is also the fair BFS batch baseline.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "hbrick/baselines/hbrick_skip_lift_baseline.hpp"
#include "hbrick/baselines/oreach_baseline.hpp"
#include "hbrick/baselines/scc_dag_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_search_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/passable_vertex_map.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/io/movingai_loader.hpp"
#include "hbrick/tile/group_size.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"
#include "hbrick/io/recipe.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct PilotArgs {
    std::filesystem::path recipe_path;
    std::filesystem::path datasets_root = "datasets/movingai";
    uint32_t b = 32U;
    uint32_t g = 4U;
    std::vector<uint32_t> k_values{4U, 8U, 16U, 32U, 64U};
    uint32_t warmup_batches = 5U;
    uint32_t repeats = 11U;
    /**
     * @brief Per-k timed pair budget shared by every method.
     *
     * When nonzero, the timed repetition count for batch size @c k becomes
     * @c total_pairs / k^2, so each method evaluates exactly @c total_pairs
     * ordered pairs at every @c k. When zero, @ref repeats is used verbatim.
     */
    uint64_t total_pairs = 0U;
    /** @brief Time and emit only the batch H-BRICK and O'Reach methods. */
    bool core_only = false;
    uint64_t seed_offset = 0U;
    double memory_gib = 8.0;
    double closure_deadline_sec = 60.0;
    bool run_scc_closure = true;
    std::filesystem::path csv_out{};
    std::filesystem::path json_out{};
    std::vector<std::pair<std::string, std::string>> meta{};
};

struct TimingSummary {
    double median_us = 0.0;
    double mean_us = 0.0;
    double min_us = 0.0;
    double max_us = 0.0;
};

/** @brief SplitMix64 for deterministic endpoint sampling. */
class Rng {
public:
    explicit Rng(const uint64_t seed) : state_(seed) {}

    uint64_t next() {
        state_ += 0x9E3779B97F4A7C15ULL;
        uint64_t z = state_;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    uint32_t uniform(const uint32_t bound) {
        return static_cast<uint32_t>(next() % bound);
    }

private:
    uint64_t state_ = 0U;
};

void printHelp(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options]\n"
        << "  --recipe <path>            Frozen recipe JSON (required)\n"
        << "  --datasets-root <path>     MovingAI map root (default: datasets/movingai)\n"
        << "  --b <size>                 H-BRICK base tile size (default 32)\n"
        << "  --g <size>                 H-BRICK group size (default 4)\n"
        << "  --k <list>                 Comma-separated batch sizes (default 4,8,16,32,64)\n"
        << "  --warmup-batches <N>       Untimed batch repetitions (default 5)\n"
        << "  --repeats <N>              Timed repetitions when --total-pairs is 0 (default 11)\n"
        << "  --total-pairs <N>          Timed pair budget per k; repetitions = N / k^2 (default 0)\n"
        << "  --core-only                Time and emit only batch H-BRICK and O'Reach\n"
        << "  --seed-offset <S>          Offset added to recipe seed for endpoint sampling\n"
        << "  --memory-gib <G>           Index memory cap in GiB (default 8.0)\n"
        << "  --closure-deadline-sec <S> SCC-DAG closure preprocessing deadline (default 60)\n"
        << "  --no-scc-closure           Skip the SCC-DAG closure baseline\n"
        << "  --csv-out <path>           Append one CSV row per (k, method)\n"
        << "  --json-out <path>          Write the full JSON result document\n"
        << "  --meta key=value           Repeatable metadata entry (git sha, cpu, ...)\n"
        << "  --help                     Show this help\n";
}

bool parseArgs(const int argc, char** argv, PilotArgs& args) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printHelp(argv[0]);
            std::exit(0);
        } else if (arg == "--recipe" && i + 1 < argc) {
            args.recipe_path = argv[++i];
        } else if (arg == "--datasets-root" && i + 1 < argc) {
            args.datasets_root = argv[++i];
        } else if (arg == "--b" && i + 1 < argc) {
            args.b = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--g" && i + 1 < argc) {
            args.g = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--k" && i + 1 < argc) {
            args.k_values.clear();
            std::stringstream stream(argv[++i]);
            std::string token;
            while (std::getline(stream, token, ',')) {
                if (!token.empty()) {
                    args.k_values.push_back(static_cast<uint32_t>(std::stoul(token)));
                }
            }
        } else if (arg == "--warmup-batches" && i + 1 < argc) {
            args.warmup_batches = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--repeats" && i + 1 < argc) {
            args.repeats = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--total-pairs" && i + 1 < argc) {
            args.total_pairs = std::stoull(argv[++i]);
        } else if (arg == "--core-only") {
            args.core_only = true;
        } else if (arg == "--seed-offset" && i + 1 < argc) {
            args.seed_offset = std::stoull(argv[++i]);
        } else if (arg == "--memory-gib" && i + 1 < argc) {
            args.memory_gib = std::stod(argv[++i]);
        } else if (arg == "--closure-deadline-sec" && i + 1 < argc) {
            args.closure_deadline_sec = std::stod(argv[++i]);
        } else if (arg == "--no-scc-closure") {
            args.run_scc_closure = false;
        } else if (arg == "--csv-out" && i + 1 < argc) {
            args.csv_out = argv[++i];
        } else if (arg == "--json-out" && i + 1 < argc) {
            args.json_out = argv[++i];
        } else if (arg == "--meta" && i + 1 < argc) {
            const std::string entry = argv[++i];
            const size_t eq = entry.find('=');
            if (eq != std::string::npos) {
                args.meta.emplace_back(entry.substr(0U, eq), entry.substr(eq + 1U));
            }
        }
    }
    if (args.recipe_path.empty()) {
        std::cerr << "Error: --recipe is required\n";
        return false;
    }
    if (args.k_values.empty()) {
        std::cerr << "Error: --k list is empty\n";
        return false;
    }
    return true;
}

std::string currentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

uint64_t fnv1aFileHash(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    uint64_t hash = 1469598103934665603ULL;
    char byte = 0;
    while (file.get(byte)) {
        hash ^= static_cast<uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

double secondsSince(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::vector<uint32_t> sampleDistinctPassable(
    Rng& rng,
    const std::vector<uint32_t>& universe,
    const uint32_t count
) {
    std::vector<uint32_t> sample;
    sample.reserve(count);
    if (universe.size() <= count) {
        return universe;
    }
    std::vector<uint8_t> taken(universe.size(), 0U);
    while (sample.size() < count) {
        const size_t candidate = rng.uniform(static_cast<uint32_t>(universe.size()));
        if (taken[candidate] == 0U) {
            taken[candidate] = 1U;
            sample.push_back(universe[candidate]);
        }
    }
    return sample;
}

/** @brief One full BFS from @p source filling @p reachable flags. */
void fillReachableFromSource(
    const hbrick::CsrGraph& graph,
    const uint32_t source,
    hbrick::GraphSearchScratch& scratch,
    std::vector<uint8_t>& reachable
) {
    std::fill(reachable.begin(), reachable.end(), uint8_t{0});
    std::vector<uint32_t>& queue = scratch.queue();
    std::vector<uint32_t>& mark = scratch.visitedMark();
    const uint32_t stamp = scratch.nextMark();
    queue.clear();
    queue.push_back(source);
    mark[source] = stamp;
    reachable[source] = 1U;
    size_t head = 0U;
    while (head < queue.size()) {
        const uint32_t vertex = queue[head++];
        for (const uint32_t neighbor : graph.outNeighbors(vertex)) {
            if (mark[neighbor] != stamp) {
                mark[neighbor] = stamp;
                reachable[neighbor] = 1U;
                queue.push_back(neighbor);
            }
        }
    }
}

TimingSummary summarize(std::vector<double> samples) {
    TimingSummary summary;
    if (samples.empty()) {
        return summary;
    }
    std::sort(samples.begin(), samples.end());
    summary.min_us = samples.front();
    summary.max_us = samples.back();
    double total = 0.0;
    for (const double value : samples) {
        total += value;
    }
    summary.mean_us = total / static_cast<double>(samples.size());
    const size_t mid = samples.size() / 2U;
    summary.median_us = (samples.size() % 2U == 0U)
        ? 0.5 * (samples[mid - 1U] + samples[mid])
        : samples[mid];
    return summary;
}

struct MethodResult {
    std::string name;
    std::string status = "COMPLETED";
    double preprocess_ms = 0.0;
    uint64_t index_bytes = 0U;
    uint64_t scratch_bytes = 0U;
    double median_us = 0.0;
    double mean_us = 0.0;
    double min_us = 0.0;
    double max_us = 0.0;
    double ns_per_pair = 0.0;
    uint32_t mismatches = 0U;
    double speedup_vs_scalar = 0.0;
    double speedup_vs_oreach = 0.0;
    double speedup_vs_bfs = 0.0;
    std::string note{};
};

/** @brief Runs one closure baseline incrementally with a wall-clock deadline. */
template <typename ClosureBaseline>
bool runClosureWithDeadline(
    ClosureBaseline& baseline,
    const double deadline_sec,
    double& elapsed_ms,
    std::string& status
) {
    const Clock::time_point start = Clock::now();
    constexpr uint32_t kPivotBatch = 512U;
    while (!baseline.stepPreprocessPivots(kPivotBatch)) {
        if (secondsSince(start) > deadline_sec) {
            baseline.abortPreprocessSkippedByPolicy();
            status = "TIMEOUT";
            elapsed_ms = secondsSince(start) * 1000.0;
            return false;
        }
    }
    elapsed_ms = secondsSince(start) * 1000.0;
    status = baseline.status() == hbrick::BaselineStatus::Completed ? "COMPLETED" : "SKIPPED";
    return baseline.status() == hbrick::BaselineStatus::Completed;
}

std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    PilotArgs args;
    if (!parseArgs(argc, argv, args)) {
        return 1;
    }

    const auto recipe_opt = hbrick::tools::loadRecipe(args.recipe_path);
    if (!recipe_opt.has_value()) {
        std::cerr << "Error: failed to parse recipe " << args.recipe_path << "\n";
        return 1;
    }
    const hbrick::tools::Recipe& recipe = *recipe_opt;

    std::filesystem::path map_path =
        args.datasets_root / recipe.set_name / "maps" / recipe.map_name;
    if (!std::filesystem::exists(map_path)) {
        map_path = args.datasets_root / recipe.map_name;
    }
    if (!std::filesystem::exists(map_path)) {
        std::cerr << "Error: map not found: " << map_path << "\n";
        return 1;
    }
    auto load_result = hbrick::loadMovingAiMap(map_path);
    if (!load_result.ok()) {
        std::cerr << "Error: failed to load map " << map_path << "\n";
        return 1;
    }
    const hbrick::MazeLayout layout = load_result.map.toMazeLayout(recipe.policy);

    hbrick::RandomAsymmetricParams orient_params{};
    orient_params.seed = recipe.seed;
    orient_params.p_bidirectional = static_cast<long double>(recipe.p_bidirectional);
    orient_params.p_one_way = static_cast<long double>(recipe.p_one_way);
    orient_params.p_edge_drop = 0.0L;
    orient_params.gradient_angle_degrees = static_cast<double>(recipe.gradient_angle_degrees);
    orient_params.p_against_gradient = static_cast<long double>(recipe.p_against_gradient);

    const hbrick::DirectedGridGraph grid_graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        recipe.mode,
        orient_params
    );
    // The frozen benchmark protocol induces a CSR over passable cells only and
    // answers generic baselines on compact ids. Match that here so endpoint
    // ids, SCC counts, and BFS results are comparable with the scalar campaign.
    const hbrick::PassableVertexMap passable_map = hbrick::PassableVertexMap::fromLayout(layout);
    const hbrick::CsrGraph csr = hbrick::inducePassableCsr(grid_graph, passable_map);

    std::vector<uint32_t> universe;
    universe.reserve(passable_map.compactVertexCount());
    const std::span<const uint32_t> passable_vertices = passable_map.passableGridVertices();
    universe.assign(passable_vertices.begin(), passable_vertices.end());

    const uint64_t memory_bytes =
        static_cast<uint64_t>(args.memory_gib * 1024.0 * 1024.0 * 1024.0);

    // --- Preprocessing (cold path) ------------------------------------------------
    std::vector<MethodResult> baseline_rows;
    hbrick::HBrickConfig hbrick_config{};
    hbrick_config.base_tile_size = hbrick::TileSize{args.b, args.b};
    hbrick_config.group_size = hbrick::GroupSize{args.g, args.g};
    hbrick_config.max_depth = hbrick::kHBrickFullDepth;
    hbrick_config.max_memory_bytes = memory_bytes;

    hbrick::HBrickSkipLiftBaseline hbrick;
    {
        const Clock::time_point start = Clock::now();
        hbrick.preprocess(grid_graph, layout, hbrick_config);
        MethodResult row;
        row.name = "h-brick-skip-lift";
        row.preprocess_ms = secondsSince(start) * 1000.0;
        row.status = hbrick.status() == hbrick::BaselineStatus::Completed ? "COMPLETED" : "FAILED";
        row.index_bytes = hbrick.indexStorageBytes();
        row.scratch_bytes = hbrick.scratchMemoryBytes();
        baseline_rows.push_back(row);
    }

    hbrick::SccDagSearchBaseline scc_search;
    hbrick::GraphSearchScratch scc_preprocess_scratch(csr.numVertices());
    double scc_search_preprocess_ms = 0.0;
    {
        const Clock::time_point start = Clock::now();
        scc_search.preprocess(csr, scc_preprocess_scratch);
        scc_search_preprocess_ms = secondsSince(start) * 1000.0;
        MethodResult row;
        row.name = "scc-dag-search";
        row.preprocess_ms = scc_search_preprocess_ms;
        row.status = scc_search.status() == hbrick::BaselineStatus::Completed ? "COMPLETED" : "FAILED";
        row.index_bytes = scc_search.indexStorageBytes();
        baseline_rows.push_back(row);
    }

    MethodResult scc_closure_row;
    scc_closure_row.name = "scc-dag-closure";
    hbrick::SccDagClosureBaseline scc_closure;
    bool scc_closure_ready = false;
    if (args.run_scc_closure && !args.core_only) {
        hbrick::GraphSearchScratch closure_scratch(csr.numVertices());
        scc_closure.beginPreprocess(csr, closure_scratch, memory_bytes, 110U);
        if (scc_closure.preprocessActive()) {
            std::string status;
            scc_closure_ready = runClosureWithDeadline(
                scc_closure,
                args.closure_deadline_sec,
                scc_closure_row.preprocess_ms,
                status
            );
            scc_closure_row.status = status;
            if (scc_closure_ready) {
                scc_closure_row.index_bytes = scc_closure.indexStorageBytes();
            }
        } else {
            scc_closure_row.status =
                scc_closure.status() == hbrick::BaselineStatus::SkippedByPolicy
                ? "SKIPPED_MEMORY_ESTIMATE"
                : "FAILED";
        }
    } else {
        scc_closure_row.status = "NOT_REQUESTED";
    }

    hbrick::OreachBaseline oreach;
    double oreach_preprocess_ms = 0.0;
    {
        const Clock::time_point start = Clock::now();
        oreach.preprocess(csr, hbrick::OreachBaselineParams{}, memory_bytes);
        oreach_preprocess_ms = secondsSince(start) * 1000.0;
        MethodResult row;
        row.name = "oreach";
        row.preprocess_ms = oreach_preprocess_ms;
        row.status = oreach.status() == hbrick::BaselineStatus::Completed ? "COMPLETED" : "FAILED";
        row.index_bytes = oreach.indexStorageBytes();
        baseline_rows.push_back(row);
    }

    hbrick::GraphSearchScratch bfs_scratch(csr.numVertices());
    hbrick::GraphSearchScratch oreach_scratch(csr.numVertices());
    hbrick::GraphSearchScratch scc_query_scratch(csr.numVertices());

    std::vector<uint8_t> bfs_truth(csr.numVertices(), 0U);

    std::ostringstream json;
    json << "{\n";
    json << "  \"timestamp\": \"" << currentTimestamp() << "\",\n";
    json << "  \"recipe\": \"" << jsonEscape(recipe.label.empty() ? recipe.map_name : recipe.label)
         << "\",\n";
    json << "  \"map\": \"" << jsonEscape(recipe.map_name) << "\",\n";
    json << "  \"set\": \"" << jsonEscape(recipe.set_name) << "\",\n";
    json << "  \"mode\": " << static_cast<int>(recipe.mode) << ",\n";
    json << "  \"recipe_seed\": " << recipe.seed << ",\n";
    json << "  \"map_fnv1a\": " << fnv1aFileHash(map_path) << ",\n";
    json << "  \"vertices\": " << csr.numVertices() << ",\n";
    json << "  \"edges\": " << csr.numEdges() << ",\n";
    json << "  \"passable\": " << universe.size() << ",\n";
    json << "  \"b\": " << args.b << ",\n";
    json << "  \"g\": " << args.g << ",\n";
    json << "  \"warmup_batches\": " << args.warmup_batches << ",\n";
    json << "  \"repeats\": " << args.repeats << ",\n";
    json << "  \"total_pairs\": " << args.total_pairs << ",\n";
    json << "  \"core_only\": " << (args.core_only ? "true" : "false") << ",\n";
    json << "  \"seed_offset\": " << args.seed_offset << ",\n";
    json << "  \"memory_gib\": " << args.memory_gib << ",\n";
    json << "  \"compiler\": \"" << jsonEscape(__VERSION__) << "\",\n";
    json << "  \"metadata\": {";
    for (size_t i = 0U; i < args.meta.size(); ++i) {
        json << (i == 0U ? "" : ", ") << "\"" << jsonEscape(args.meta[i].first) << "\": \""
             << jsonEscape(args.meta[i].second) << "\"";
    }
    json << "},\n";
    json << "  \"baselines\": [\n";
    for (size_t i = 0U; i < baseline_rows.size(); ++i) {
        const MethodResult& row = baseline_rows[i];
        json << "    {\"method\": \"" << row.name << "\", \"status\": \"" << row.status
             << "\", \"preprocess_ms\": " << row.preprocess_ms
             << ", \"index_bytes\": " << row.index_bytes
             << ", \"scratch_bytes\": " << row.scratch_bytes << "}"
             << (i + 1U == baseline_rows.size() ? "" : ",") << "\n";
    }
    json << "  ],\n";
    json << "  \"scc_closure\": {\"status\": \"" << scc_closure_row.status
         << "\", \"preprocess_ms\": " << scc_closure_row.preprocess_ms
         << ", \"index_bytes\": " << scc_closure_row.index_bytes
         << ", \"components\": " << scc_closure.numComponents() << "},\n";
    json << "  \"k_results\": [\n";

    bool csv_header_written = false;
    std::ofstream csv;
    if (!args.csv_out.empty()) {
        const bool exists = std::filesystem::exists(args.csv_out);
        csv.open(args.csv_out, std::ios::app);
        csv_header_written = exists;
    }

    for (size_t k_index = 0U; k_index < args.k_values.size(); ++k_index) {
        const uint32_t k = args.k_values[k_index];
        if (k == 0U || k > universe.size()) {
            continue;
        }
        Rng rng(recipe.seed ^ (0x9E3779B97F4A7C15ULL * (k + 1U)) ^ (args.seed_offset + 1U));
        const std::vector<uint32_t> sources = sampleDistinctPassable(rng, universe, k);
        const std::vector<uint32_t> targets = sampleDistinctPassable(rng, universe, k);
        const size_t pairs = sources.size() * targets.size();

        // Equalize the timed pair volume across k: each method evaluates
        // exactly args.total_pairs ordered pairs. k^2 divides the 16*4096
        // budget for every requested k, so the division is exact here.
        bool pair_budget_satisfied = true;
        uint32_t k_repeats = args.repeats;
        if (args.total_pairs > 0U && pairs > 0U) {
            const uint64_t counted = args.total_pairs / static_cast<uint64_t>(pairs);
            k_repeats = static_cast<uint32_t>(counted > UINT32_MAX ? UINT32_MAX : counted);
            if (k_repeats == 0U) {
                k_repeats = 1U;
            }
            pair_budget_satisfied =
                (static_cast<uint64_t>(pairs) * k_repeats == args.total_pairs);
            if (!pair_budget_satisfied) {
                std::cerr << "Warning: --total-pairs " << args.total_pairs
                          << " is not a multiple of k^2=" << pairs
                          << " for k=" << k << "; running "
                          << (static_cast<uint64_t>(pairs) * k_repeats)
                          << " pairs instead\n";
            }
        }

        if (!hbrick.prepareBatch(k, k)) {
            std::cerr << "Error: prepareBatch failed for k=" << k << "\n";
            return 1;
        }

        std::vector<uint32_t> sources_compact(sources.size(), 0U);
        std::vector<uint32_t> targets_compact(targets.size(), 0U);
        for (size_t i = 0U; i < sources.size(); ++i) {
            sources_compact[i] = passable_map.toCompact(sources[i]);
        }
        for (size_t j = 0U; j < targets.size(); ++j) {
            targets_compact[j] = passable_map.toCompact(targets[j]);
        }

        std::vector<uint8_t> out_batch(pairs, 0U);
        std::vector<uint8_t> out_scalar(pairs, 0U);
        std::vector<uint8_t> out_oreach(pairs, 0U);
        std::vector<uint8_t> out_scc(pairs, 0U);
        std::vector<uint8_t> out_bfs(pairs, 0U);

        // Fresh-batch correctness run (also primes caches).
        hbrick::HBrickBatchQueryStats batch_stats{};
        {
            std::vector<uint8_t> batch_out(pairs, 0U);
            hbrick::HBrickBatchQueryStats stats{};
            hbrick.batchQuery(sources, targets, batch_out, &stats);
            batch_stats = stats;
            for (size_t i = 0U; i < sources.size(); ++i) {
                for (size_t j = 0U; j < targets.size(); ++j) {
                    out_batch[i * targets.size() + j] = batch_out[i * targets.size() + j];
                }
            }
        }
        if (!args.core_only) {
            for (size_t i = 0U; i < sources.size(); ++i) {
                for (size_t j = 0U; j < targets.size(); ++j) {
                    out_scalar[i * targets.size() + j] =
                        hbrick.query(sources[i], targets[j]) == hbrick::ReachabilityAnswer::Reachable
                        ? 1U
                        : 0U;
                }
            }
        }
        for (size_t i = 0U; i < sources.size(); ++i) {
            fillReachableFromSource(csr, sources_compact[i], bfs_scratch, bfs_truth);
            for (size_t j = 0U; j < targets.size(); ++j) {
                out_bfs[i * targets.size() + j] = bfs_truth[targets_compact[j]];
            }
        }
        uint32_t oreach_settled_by_observation = 0U;
        for (size_t i = 0U; i < sources.size(); ++i) {
            for (size_t j = 0U; j < targets.size(); ++j) {
                const hbrick::OreachQueryOutcome outcome = oreach.queryDetailed(
                    sources_compact[i],
                    targets_compact[j],
                    oreach_scratch
                );
                out_oreach[i * targets.size() + j] =
                    outcome.answer == hbrick::ReachabilityAnswer::Reachable ? 1U : 0U;
                oreach_settled_by_observation += outcome.settled_by_observation ? 1U : 0U;
            }
        }
        const double oreach_observation_rate = pairs > 0U
            ? static_cast<double>(oreach_settled_by_observation) / static_cast<double>(pairs)
            : 0.0;
        if (!args.core_only) {
            for (size_t i = 0U; i < sources.size(); ++i) {
                for (size_t j = 0U; j < targets.size(); ++j) {
                    out_scc[i * targets.size() + j] =
                        scc_search.query(sources_compact[i], targets_compact[j], scc_query_scratch)
                            == hbrick::ReachabilityAnswer::Reachable
                        ? 1U
                        : 0U;
                }
            }
        }

        std::vector<MethodResult> rows;

        const auto countMismatches = [](const std::vector<uint8_t>& left,
                                        const std::vector<uint8_t>& right) {
            uint32_t mismatches = 0U;
            for (size_t index = 0U; index < left.size(); ++index) {
                mismatches += left[index] != right[index] ? 1U : 0U;
            }
            return mismatches;
        };

        // --- Batch H-BRICK --------------------------------------------------------
        {
            MethodResult row;
            row.name = "batch-h-brick";
            row.scratch_bytes = hbrick.batchScratchMemoryBytes();
            row.index_bytes = hbrick.indexStorageBytes();
            row.preprocess_ms = baseline_rows.front().preprocess_ms;
            row.mismatches = countMismatches(out_batch, out_bfs);
            row.status = row.mismatches == 0U ? "COMPLETED" : "MISMATCH";
            std::vector<double> samples;
            samples.reserve(k_repeats);
            for (uint32_t warm = 0U; warm < args.warmup_batches; ++warm) {
                hbrick.batchQuery(sources, targets, out_batch, nullptr);
            }
            for (uint32_t rep = 0U; rep < k_repeats; ++rep) {
                const Clock::time_point start = Clock::now();
                hbrick.batchQuery(sources, targets, out_batch, nullptr);
                samples.push_back(secondsSince(start) * 1e6);
            }
            const TimingSummary summary = summarize(std::move(samples));
            row.median_us = summary.median_us;
            row.mean_us = summary.mean_us;
            row.min_us = summary.min_us;
            row.max_us = summary.max_us;
            row.ns_per_pair = summary.median_us * 1000.0 / static_cast<double>(pairs);
            rows.push_back(row);
        }

        // --- Scalar H-BRICK -------------------------------------------------------
        if (!args.core_only) {
            MethodResult row;
            row.name = "scalar-h-brick";
            row.scratch_bytes = hbrick.scratchMemoryBytes();
            row.index_bytes = hbrick.indexStorageBytes();
            row.preprocess_ms = baseline_rows.front().preprocess_ms;
            row.mismatches = countMismatches(out_scalar, out_bfs);
            row.status = row.mismatches == 0U ? "COMPLETED" : "MISMATCH";
            const auto run = [&]() {
                uint32_t reachable = 0U;
                for (size_t i = 0U; i < sources.size(); ++i) {
                    for (size_t j = 0U; j < targets.size(); ++j) {
                        reachable += hbrick.query(sources[i], targets[j])
                                == hbrick::ReachabilityAnswer::Reachable
                            ? 1U
                            : 0U;
                    }
                }
                return reachable;
            };
            for (uint32_t warm = 0U; warm < args.warmup_batches; ++warm) {
                (void)run();
            }
            std::vector<double> samples;
            samples.reserve(k_repeats);
            for (uint32_t rep = 0U; rep < k_repeats; ++rep) {
                const Clock::time_point start = Clock::now();
                (void)run();
                samples.push_back(secondsSince(start) * 1e6);
            }
            const TimingSummary summary = summarize(std::move(samples));
            row.median_us = summary.median_us;
            row.mean_us = summary.mean_us;
            row.min_us = summary.min_us;
            row.max_us = summary.max_us;
            row.ns_per_pair = summary.median_us * 1000.0 / static_cast<double>(pairs);
            rows.push_back(row);
        }

        // --- O'Reach --------------------------------------------------------------
        {
            MethodResult row;
            row.name = "oreach";
            row.preprocess_ms = oreach_preprocess_ms;
            row.index_bytes = oreach.indexStorageBytes();
            row.mismatches = countMismatches(out_oreach, out_bfs);
            row.status = row.mismatches == 0U ? "COMPLETED" : "MISMATCH";
            const auto run = [&]() {
                uint32_t reachable = 0U;
                for (size_t i = 0U; i < sources.size(); ++i) {
                    for (size_t j = 0U; j < targets.size(); ++j) {
                        reachable += oreach.query(sources_compact[i], targets_compact[j], oreach_scratch)
                                == hbrick::ReachabilityAnswer::Reachable
                            ? 1U
                            : 0U;
                    }
                }
                return reachable;
            };
            for (uint32_t warm = 0U; warm < args.warmup_batches; ++warm) {
                (void)run();
            }
            std::vector<double> samples;
            samples.reserve(k_repeats);
            for (uint32_t rep = 0U; rep < k_repeats; ++rep) {
                const Clock::time_point start = Clock::now();
                (void)run();
                samples.push_back(secondsSince(start) * 1e6);
            }
            const TimingSummary summary = summarize(std::move(samples));
            row.median_us = summary.median_us;
            row.mean_us = summary.mean_us;
            row.min_us = summary.min_us;
            row.max_us = summary.max_us;
            row.ns_per_pair = summary.median_us * 1000.0 / static_cast<double>(pairs);
            rows.push_back(row);
        }

        // --- BFS: one traversal per distinct source -------------------------------
        if (!args.core_only) {
            MethodResult row;
            row.name = "bfs-per-source";
            row.mismatches = countMismatches(out_bfs, out_bfs);
            row.status = "COMPLETED";
            const auto run = [&]() {
                uint32_t reachable = 0U;
                for (size_t i = 0U; i < sources.size(); ++i) {
                    fillReachableFromSource(csr, sources_compact[i], bfs_scratch, bfs_truth);
                    for (size_t j = 0U; j < targets.size(); ++j) {
                        reachable += bfs_truth[targets_compact[j]];
                    }
                }
                return reachable;
            };
            for (uint32_t warm = 0U; warm < args.warmup_batches; ++warm) {
                (void)run();
            }
            std::vector<double> samples;
            samples.reserve(k_repeats);
            for (uint32_t rep = 0U; rep < k_repeats; ++rep) {
                const Clock::time_point start = Clock::now();
                (void)run();
                samples.push_back(secondsSince(start) * 1e6);
            }
            const TimingSummary summary = summarize(std::move(samples));
            row.median_us = summary.median_us;
            row.mean_us = summary.mean_us;
            row.min_us = summary.min_us;
            row.max_us = summary.max_us;
            row.ns_per_pair = summary.median_us * 1000.0 / static_cast<double>(pairs);
            rows.push_back(row);
        }

        // --- SCC-DAG search: one traversal per distinct source component ----------
        if (!args.core_only && scc_search.status() == hbrick::BaselineStatus::Completed) {
            MethodResult row;
            row.name = "scc-dag-search";
            row.index_bytes = scc_search.indexStorageBytes();
            row.preprocess_ms = scc_search_preprocess_ms;
            row.mismatches = countMismatches(out_scc, out_bfs);
            row.status = row.mismatches == 0U ? "COMPLETED" : "MISMATCH";
            const hbrick::CsrGraph& dag = scc_search.condensationDag();
            std::vector<uint8_t> dag_reached(dag.numVertices(), 0U);
            const auto run = [&]() {
                uint32_t reachable = 0U;
                for (size_t i = 0U; i < sources.size(); ++i) {
                    const uint32_t source_component = scc_search.componentOf(sources_compact[i]);
                    if (source_component == std::numeric_limits<uint32_t>::max()) {
                        continue;
                    }
                    std::fill(dag_reached.begin(), dag_reached.end(), uint8_t{0});
                    std::vector<uint32_t>& queue = scc_query_scratch.queue();
                    queue.clear();
                    queue.push_back(source_component);
                    dag_reached[source_component] = 1U;
                    size_t head = 0U;
                    while (head < queue.size()) {
                        const uint32_t component = queue[head++];
                        for (const uint32_t next : dag.outNeighbors(component)) {
                            if (dag_reached[next] == 0U) {
                                dag_reached[next] = 1U;
                                queue.push_back(next);
                            }
                        }
                    }
                    for (size_t j = 0U; j < targets.size(); ++j) {
                        const uint32_t target_component = scc_search.componentOf(targets_compact[j]);
                        reachable += target_component != std::numeric_limits<uint32_t>::max()
                                && dag_reached[target_component] != 0U
                            ? 1U
                            : 0U;
                    }
                }
                return reachable;
            };
            for (uint32_t warm = 0U; warm < args.warmup_batches; ++warm) {
                (void)run();
            }
            std::vector<double> samples;
            samples.reserve(k_repeats);
            for (uint32_t rep = 0U; rep < k_repeats; ++rep) {
                const Clock::time_point start = Clock::now();
                (void)run();
                samples.push_back(secondsSince(start) * 1e6);
            }
            const TimingSummary summary = summarize(std::move(samples));
            row.median_us = summary.median_us;
            row.mean_us = summary.mean_us;
            row.min_us = summary.min_us;
            row.max_us = summary.max_us;
            row.ns_per_pair = summary.median_us * 1000.0 / static_cast<double>(pairs);
            rows.push_back(row);
        }

        // --- SCC-DAG closure lookups ----------------------------------------------
        if (!args.core_only && scc_closure_ready) {
            MethodResult row;
            row.name = "scc-dag-closure";
            row.index_bytes = scc_closure.indexStorageBytes();
            row.preprocess_ms = scc_closure_row.preprocess_ms;
            const auto run = [&]() {
                uint32_t reachable = 0U;
                for (size_t i = 0U; i < sources.size(); ++i) {
                    for (size_t j = 0U; j < targets.size(); ++j) {
                        reachable += scc_closure.query(sources_compact[i], targets_compact[j])
                                == hbrick::ReachabilityAnswer::Reachable
                            ? 1U
                            : 0U;
                    }
                }
                return reachable;
            };
            uint32_t closure_mismatches = 0U;
            for (size_t i = 0U; i < sources.size(); ++i) {
                for (size_t j = 0U; j < targets.size(); ++j) {
                    const uint8_t answer =
                        scc_closure.query(sources_compact[i], targets_compact[j])
                            == hbrick::ReachabilityAnswer::Reachable
                        ? 1U
                        : 0U;
                    closure_mismatches +=
                        answer != out_bfs[i * targets.size() + j] ? 1U : 0U;
                }
            }
            row.mismatches = closure_mismatches;
            row.status = closure_mismatches == 0U ? "COMPLETED" : "MISMATCH";
            for (uint32_t warm = 0U; warm < args.warmup_batches; ++warm) {
                (void)run();
            }
            std::vector<double> samples;
            samples.reserve(k_repeats);
            for (uint32_t rep = 0U; rep < k_repeats; ++rep) {
                const Clock::time_point start = Clock::now();
                (void)run();
                samples.push_back(secondsSince(start) * 1e6);
            }
            const TimingSummary summary = summarize(std::move(samples));
            row.median_us = summary.median_us;
            row.mean_us = summary.mean_us;
            row.min_us = summary.min_us;
            row.max_us = summary.max_us;
            row.ns_per_pair = summary.median_us * 1000.0 / static_cast<double>(pairs);
            rows.push_back(row);
        }

        const auto findRow = [&](const std::string& name) -> const MethodResult* {
            for (const MethodResult& row : rows) {
                if (row.name == name) {
                    return &row;
                }
            }
            return nullptr;
        };
        const MethodResult* scalar_row = findRow("scalar-h-brick");
        const MethodResult* oreach_row = findRow("oreach");
        const MethodResult* bfs_row = findRow("bfs-per-source");
        for (MethodResult& row : rows) {
            if (scalar_row != nullptr && row.median_us > 0.0) {
                row.speedup_vs_scalar = scalar_row->median_us / row.median_us;
            }
            if (oreach_row != nullptr && row.median_us > 0.0) {
                row.speedup_vs_oreach = oreach_row->median_us / row.median_us;
            }
            if (bfs_row != nullptr && row.median_us > 0.0) {
                row.speedup_vs_bfs = bfs_row->median_us / row.median_us;
            }
        }

        // --- Emit JSON for this k -------------------------------------------------
        json << "    {\"k\": " << k << ", \"pairs\": " << pairs
             << ", \"repeats\": " << k_repeats
             << ", \"pair_budget\": " << args.total_pairs
             << ", \"pair_budget_satisfied\": "
             << (pair_budget_satisfied ? "true" : "false")
             << ", \"pairs_performed\": " << (static_cast<uint64_t>(pairs) * k_repeats)
             << ", \"sources\": [";
        for (size_t i = 0U; i < sources.size(); ++i) {
            json << (i == 0U ? "" : ", ") << sources[i];
        }
        json << "], \"targets\": [";
        for (size_t i = 0U; i < targets.size(); ++i) {
            json << (i == 0U ? "" : ", ") << targets[i];
        }
        json << "], \"methods\": [";
        for (size_t i = 0U; i < rows.size(); ++i) {
            const MethodResult& row = rows[i];
            json << (i == 0U ? "\n" : ",\n") << "      {\"method\": \"" << row.name
                 << "\", \"status\": \"" << row.status << "\""
                 << ", \"preprocess_ms\": " << row.preprocess_ms
                 << ", \"index_bytes\": " << row.index_bytes
                 << ", \"scratch_bytes\": " << row.scratch_bytes
                 << ", \"median_us\": " << row.median_us
                 << ", \"mean_us\": " << row.mean_us
                 << ", \"min_us\": " << row.min_us
                 << ", \"max_us\": " << row.max_us
                 << ", \"ns_per_pair\": " << row.ns_per_pair
                 << ", \"mismatches\": " << row.mismatches
                 << ", \"speedup_vs_scalar\": " << row.speedup_vs_scalar
                 << ", \"speedup_vs_oreach\": " << row.speedup_vs_oreach
                 << ", \"speedup_vs_bfs\": " << row.speedup_vs_bfs
                 << ((row.name == "oreach")
                         ? (std::string(", \"oreach_observation_rate\": ")
                                + std::to_string(oreach_observation_rate))
                         : std::string())
                 << ((row.name == "batch-h-brick")
                         ? (std::string(", \"batch_local_hits\": ")
                                + std::to_string(batch_stats.local_hits)
                                + ", \"batch_ancestor_hits\": "
                                + std::to_string(batch_stats.ancestor_hits)
                                + ", \"batch_fallbacks\": "
                                + std::to_string(batch_stats.fallbacks)
                                + ", \"batch_hierarchy_negatives\": "
                                + std::to_string(batch_stats.hierarchy_negatives)
                                + ", \"batch_source_lifts\": "
                                + std::to_string(batch_stats.source_lifts)
                                + ", \"batch_target_lifts\": "
                                + std::to_string(batch_stats.target_lifts)
                                + ", \"batch_source_projections\": "
                                + std::to_string(batch_stats.source_projections)
                                + ", \"batch_pair_tests\": "
                                + std::to_string(batch_stats.pair_tests)
                                + ", \"batch_max_level\": "
                                + std::to_string(batch_stats.max_level_reached))
                         : std::string())
                 << "}";
        }
        json << "\n    ]}";
        json << (k_index + 1U == args.k_values.size() ? "\n" : ",\n");

        // --- Emit CSV rows --------------------------------------------------------
        if (csv.is_open()) {
            if (!csv_header_written) {
                csv << "timestamp,recipe,map,set,orientation_seed,b,g,k,method,status,"
                       "preprocess_ms,index_bytes,scratch_bytes,median_us,mean_us,min_us,"
                       "max_us,ns_per_pair,speedup_vs_scalar,speedup_vs_oreach,"
                       "speedup_vs_bfs,mismatches\n";
                csv_header_written = true;
            }
            for (const MethodResult& row : rows) {
                csv << currentTimestamp() << ',' << (recipe.label.empty() ? recipe.map_name : recipe.label)
                    << ',' << recipe.map_name << ',' << recipe.set_name << ',' << recipe.seed
                    << ',' << args.b << ',' << args.g << ',' << k << ',' << row.name << ','
                    << row.status << ',' << row.preprocess_ms << ',' << row.index_bytes << ','
                    << row.scratch_bytes << ',' << row.median_us << ',' << row.mean_us << ','
                    << row.min_us << ',' << row.max_us << ',' << row.ns_per_pair << ','
                    << row.speedup_vs_scalar << ',' << row.speedup_vs_oreach << ','
                    << row.speedup_vs_bfs << ',' << row.mismatches << '\n';
            }
        }

        std::cerr << "PROGRESS k=" << k << " batch_median_us="
                  << (findRow("batch-h-brick") != nullptr ? findRow("batch-h-brick")->median_us : 0.0)
                  << " scalar_median_us="
                  << (scalar_row != nullptr ? scalar_row->median_us : 0.0) << "\n";
    }

    json << "  ]\n}\n";

    if (!args.json_out.empty()) {
        std::ofstream out(args.json_out);
        out << json.str();
    }
    std::cout << json.str();
    return 0;
}
