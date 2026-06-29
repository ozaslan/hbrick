#include "hbrick/bench/benchmark_campaign_run.hpp"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "hbrick/bench/benchmark_campaign_config.hpp"
#include "hbrick/bench/benchmark_campaign_dataset.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/io/movingai_loader.hpp"
#include "maze_generator.hpp"
#include "recipe.hpp"

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

[[nodiscard]] uint64_t parseU64(const std::string& text) {
    return static_cast<uint64_t>(std::stoull(text));
}

[[nodiscard]] uint32_t parseU32(const std::string& text) {
    return static_cast<uint32_t>(std::stoul(text));
}

[[nodiscard]] float parseFloat(const std::string& text) {
    return std::stof(text);
}

[[nodiscard]] bool parseManifestRow(
    const std::vector<std::string>& fields,
    BenchmarkCampaignManifestEntry& entry
) {
    if (fields.size() < 8U) {
        return false;
    }
    if (fields[0] != kBenchmarkCampaignSchemaVersion) {
        return false;
    }

    entry.map.map_id = fields[2];
    entry.map.generator_type = fields[3];
    entry.map.recipe_path = fields[4];
    entry.map.gallery_image_path = fields[5];

    if (fields.size() < 26U) {
        if (fields.size() >= 8U) {
            entry.characterization.num_vertices = parseU32(fields[6]);
            entry.characterization.num_edges = parseU64(fields[7]);
        }
        return true;
    }

    entry.characterization.grid_width = parseU32(fields[6]);
    entry.characterization.grid_height = parseU32(fields[7]);
    entry.characterization.passable_cells = parseU32(fields[8]);
    entry.characterization.num_vertices = parseU32(fields[9]);
    entry.characterization.num_edges = parseU64(fields[10]);
    entry.characterization.undirected_component_count = parseU32(fields[11]);
    entry.characterization.largest_undirected_component = parseU32(fields[12]);
    entry.characterization.num_strongly_connected_components = parseU32(fields[13]);
    entry.characterization.largest_strongly_connected_component = parseU32(fields[14]);
    entry.characterization.condensation_vertices = parseU32(fields[15]);
    entry.characterization.condensation_edges = parseU32(fields[16]);
    entry.characterization.orientation_mode = fields[17];
    entry.characterization.carve_seed = parseU64(fields[18]);
    entry.characterization.opening_seed = parseU64(fields[19]);
    entry.characterization.extra_openings = parseU32(fields[20]);
    entry.characterization.p_one_way = parseFloat(fields[21]);
    entry.characterization.p_bidirectional = parseFloat(fields[22]);
    entry.characterization.gradient_angle_degrees = parseFloat(fields[23]);
    entry.characterization.p_against_gradient = parseFloat(fields[24]);
    entry.characterization.orientation_seed = parseU64(fields[25]);
    if (fields.size() > 26U) {
        entry.characterization.gallery_image_hash = parseU64(fields[26]);
    }
    if (fields.size() > 27U) {
        entry.characterization.movingai_set = fields[27];
    }
    if (fields.size() > 28U) {
        entry.characterization.movingai_map = fields[28];
    }
    if (fields.size() > 29U) {
        entry.characterization.datasets_root = fields[29];
    }
    if (fields.size() > 30U) {
        entry.characterization.passability_policy = fields[30];
    }
    return true;
}

using CompletedMethodMap =
    std::unordered_map<std::string, std::unordered_set<std::string>>;

[[nodiscard]] CompletedMethodMap completedMethodsFromResults(
    const std::filesystem::path& results_csv
) {
    CompletedMethodMap completed;
    std::ifstream input(results_csv);
    if (!input.is_open()) {
        return completed;
    }

    std::string line;
    if (!std::getline(input, line)) {
        return completed;
    }

    std::vector<std::string> fields;
    while (std::getline(input, line)) {
        if (!splitCsvLine(line, fields) || fields.size() < 6U) {
            continue;
        }
        if (fields[5] == "Completed") {
            completed[fields[3]].insert(fields[4]);
        }
    }
    return completed;
}

[[nodiscard]] bool allMethodsCompleted(
    const std::string& map_id,
    const std::span<const ReachabilityBaselineId> methods,
    const CompletedMethodMap& completed
) {
    const auto iterator = completed.find(map_id);
    if (iterator == completed.end()) {
        return false;
    }
    for (const ReachabilityBaselineId method : methods) {
        if (iterator->second.count(reachabilityBaselineName(method)) == 0U) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool mapMatchesFilter(
    const std::string& map_id,
    const std::vector<std::string>& filter
) {
    if (filter.empty()) {
        return true;
    }
    for (const std::string& candidate : filter) {
        if (candidate == map_id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] GridEdgeConversionMode modeFromLabel(const std::string& label) {
    GridEdgeConversionMode mode = GridEdgeConversionMode::RandomAsymmetric;
    if (label == "bidirectional_all") {
        mode = GridEdgeConversionMode::BidirectionalAll;
    } else if (label == "acyclic_east_south") {
        mode = GridEdgeConversionMode::AcyclicEastSouth;
    } else if (label == "gradient_flow") {
        mode = GridEdgeConversionMode::GradientFlow;
    }
    return mode;
}

[[nodiscard]] bool buildGraphFromRecipe(
    const tools::Recipe& recipe,
    const std::filesystem::path& datasets_root,
    MazeLayout& layout,
    DirectedGridGraph& graph,
    std::string& error_message
) {
    const std::filesystem::path map_path =
        datasets_root / recipe.set_name / "maps" / recipe.map_name;
    const MovingAiLoadResult loaded = loadMovingAiMap(map_path);
    if (!loaded.ok()) {
        error_message = "Failed to load MovingAI map: " + map_path.string();
        return false;
    }

    layout = loaded.map.toMazeLayout(recipe.policy);
    RandomAsymmetricParams params{};
    params.seed = recipe.seed;
    params.p_one_way = recipe.p_one_way;
    params.p_bidirectional = recipe.p_bidirectional;
    params.gradient_angle_degrees = recipe.gradient_angle_degrees;
    params.p_against_gradient = recipe.p_against_gradient;
    params = sanitizeRandomAsymmetricParams(params);
    graph = DirectedGridGraphBuilder::build(layout, recipe.mode, params);
    return true;
}

}  // namespace

bool loadBenchmarkCampaignManifest(
    const std::filesystem::path& manifest_csv,
    std::vector<BenchmarkCampaignManifestEntry>& entries,
    std::string& error_message
) {
    entries.clear();
    std::ifstream input(manifest_csv);
    if (!input.is_open()) {
        error_message = "Failed to open manifest: " + manifest_csv.string();
        return false;
    }

    std::string line;
    if (!std::getline(input, line)) {
        error_message = "Manifest is empty";
        return false;
    }

    std::vector<std::string> fields;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        if (!splitCsvLine(line, fields)) {
            error_message = "Malformed CSV line in manifest";
            return false;
        }
        BenchmarkCampaignManifestEntry entry{};
        if (!parseManifestRow(fields, entry)) {
            continue;
        }
        entries.push_back(entry);
    }
    return true;
}

bool rebuildProceduralCampaignMap(
    const BenchmarkCampaignManifestEntry& entry,
    MazeLayout& layout,
    DirectedGridGraph& graph,
    std::string& error_message
) {
    if (entry.map.generator_type != "procedural_maze") {
        error_message = "Unsupported generator type: " + entry.map.generator_type;
        return false;
    }

    const BenchmarkCampaignMapCharacterization& stats = entry.characterization;
    if (stats.grid_width == 0U || stats.grid_height == 0U) {
        error_message = "Manifest row missing procedural dimensions for " + entry.map.map_id;
        return false;
    }

    const uint32_t logical_width = (stats.grid_width - 1U) / 2U;
    const uint32_t logical_height = (stats.grid_height - 1U) / 2U;
    const test_support::MazeParams maze_params{
        logical_width,
        logical_height,
        stats.carve_seed,
    };
    layout = test_support::generateMazeWithExtraPassages(
        maze_params,
        stats.opening_seed,
        stats.extra_openings
    );

    RandomAsymmetricParams params{};
    params.seed = stats.orientation_seed;
    params.p_one_way = stats.p_one_way;
    params.p_bidirectional = stats.p_bidirectional;
    params.gradient_angle_degrees = stats.gradient_angle_degrees;
    params.p_against_gradient = stats.p_against_gradient;
    params = sanitizeRandomAsymmetricParams(params);

    graph = DirectedGridGraphBuilder::build(
        layout,
        modeFromLabel(stats.orientation_mode),
        params
    );
    return true;
}

bool rebuildCampaignMapFromManifestEntry(
    const BenchmarkCampaignManifestEntry& entry,
    const std::filesystem::path& datasets_root_override,
    MazeLayout& layout,
    DirectedGridGraph& graph,
    std::string& error_message
) {
    if (entry.map.generator_type == "procedural_maze") {
        return rebuildProceduralCampaignMap(entry, layout, graph, error_message);
    }
    if (entry.map.generator_type != "movingai") {
        error_message = "Unsupported generator type: " + entry.map.generator_type;
        return false;
    }

    if (entry.map.recipe_path.empty()) {
        error_message = "MovingAI manifest row missing recipe_path for " + entry.map.map_id;
        return false;
    }

    const std::optional<tools::Recipe> recipe =
        tools::loadRecipe(entry.map.recipe_path);
    if (!recipe.has_value()) {
        error_message = "Failed to load recipe: " + entry.map.recipe_path;
        return false;
    }

    std::filesystem::path datasets_root = datasets_root_override;
    if (datasets_root.empty() && !entry.characterization.datasets_root.empty()) {
        datasets_root = entry.characterization.datasets_root;
    }
    if (datasets_root.empty()) {
        datasets_root = defaultMovingAiDatasetsRoot();
    }
    if (datasets_root.empty()) {
        error_message =
            "MovingAI datasets root not set; pass --datasets-root or store it in the manifest";
        return false;
    }

    return buildGraphFromRecipe(*recipe, datasets_root, layout, graph, error_message);
}

bool runBenchmarkCampaignFromManifest(
    const BenchmarkCampaignPaths& paths,
    const BenchmarkCampaignMetadata& metadata,
    ReachabilityBenchmarkConfig config,
    const BenchmarkCampaignRunOptions& options,
    std::string& error_message
) {
    std::vector<BenchmarkCampaignManifestEntry> entries;
    if (!loadBenchmarkCampaignManifest(paths.manifest_csv, entries, error_message)) {
        return false;
    }
    if (entries.empty()) {
        error_message = "Manifest contains no runnable rows";
        return false;
    }

    if (config.methods.empty()) {
        config = benchmarkCampaignConfigFromPreset("all");
    }

    const CompletedMethodMap completed =
        options.resume
            ? completedMethodsFromResults(paths.results_csv)
            : CompletedMethodMap{};

    bool ran_any = false;
    for (const BenchmarkCampaignManifestEntry& entry : entries) {
        if (!mapMatchesFilter(entry.map.map_id, options.map_ids)) {
            continue;
        }
        if (options.resume
            && allMethodsCompleted(entry.map.map_id, config.methods, completed)) {
            if (options.logger != nullptr) {
                options.logger->infof(
                    "Skipping %s (all methods already completed)",
                    entry.map.map_id.c_str()
                );
            }
            continue;
        }

        if (options.logger != nullptr) {
            options.logger->infof(
                "Running manifest row %s (%s)",
                entry.map.map_id.c_str(),
                entry.map.generator_type.c_str()
            );
        }

        MazeLayout layout{1U, 1U, false};
        DirectedGridGraph graph{};
        if (!rebuildCampaignMapFromManifestEntry(
                entry,
                options.datasets_root_override,
                layout,
                graph,
                error_message)) {
            if (options.logger != nullptr) {
                options.logger->error(error_message.c_str());
            }
            return false;
        }

        if (!runBenchmarkCampaignGridJob(
                paths,
                metadata,
                entry.map,
                graph,
                layout,
                config,
                error_message,
                false,
                options.logger)) {
            return false;
        }
        ran_any = true;
    }

    if (!ran_any) {
        error_message = "No manifest rows matched filters or all were skipped";
        return false;
    }
    return true;
}

}  // namespace hbrick
