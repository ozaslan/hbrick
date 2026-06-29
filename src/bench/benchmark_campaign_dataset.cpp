#include "hbrick/bench/benchmark_campaign_dataset.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

#include "hbrick/bench/benchmark_campaign_gallery.hpp"
#include "hbrick/graph/condensation_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/kleene_squaring_bounds.hpp"
#include "hbrick/graph/scc_decomposition.hpp"
#include "hbrick/io/movingai_loader.hpp"
#include "maze_generator.hpp"
#include "movingai_map_catalog.hpp"
#include "recipe.hpp"

namespace hbrick {

namespace {

void countPassableUndirectedComponents(
    const MazeLayout& layout,
    uint32_t& component_count,
    uint32_t& largest_component
) noexcept {
    component_count = 0U;
    largest_component = 0U;

    const uint32_t width = layout.width();
    const uint32_t height = layout.height();
    if (width == 0U || height == 0U) {
        return;
    }

    std::vector<uint8_t> visited(width * height, 0U);
    std::vector<uint32_t> stack;
    stack.reserve(width * height);

    for (uint32_t y = 0U; y < height; ++y) {
        for (uint32_t x = 0U; x < width; ++x) {
            if (!layout.isPassable(x, y)) {
                continue;
            }
            const uint32_t start = y * width + x;
            if (visited[start] != 0U) {
                continue;
            }

            ++component_count;
            uint32_t component_size = 0U;
            stack.clear();
            stack.push_back(start);
            visited[start] = 1U;

            while (!stack.empty()) {
                const uint32_t cell = stack.back();
                stack.pop_back();
                ++component_size;

                const uint32_t cell_x = cell % width;
                const uint32_t cell_y = cell / width;
                static constexpr int kDx[4] = {1, -1, 0, 0};
                static constexpr int kDy[4] = {0, 0, 1, -1};
                for (int direction = 0; direction < 4; ++direction) {
                    const int neighbor_x =
                        static_cast<int>(cell_x) + kDx[direction];
                    const int neighbor_y =
                        static_cast<int>(cell_y) + kDy[direction];
                    if (neighbor_x < 0 || neighbor_y < 0) {
                        continue;
                    }
                    const uint32_t nx = static_cast<uint32_t>(neighbor_x);
                    const uint32_t ny = static_cast<uint32_t>(neighbor_y);
                    if (nx >= width || ny >= height) {
                        continue;
                    }
                    if (!layout.isPassable(nx, ny)) {
                        continue;
                    }
                    const uint32_t neighbor = ny * width + nx;
                    if (visited[neighbor] != 0U) {
                        continue;
                    }
                    visited[neighbor] = 1U;
                    stack.push_back(neighbor);
                }
            }

            largest_component = std::max(largest_component, component_size);
        }
    }
}

[[nodiscard]] tools::Recipe recipeFromProceduralSpec(
    const ProceduralMazeSpec& spec,
    const std::string& campaign_id,
    const std::string& map_id
) {
    tools::Recipe recipe{};
    recipe.set_name = campaign_id;
    recipe.map_name = map_id;
    recipe.mode = spec.orientation_mode;
    recipe.seed = spec.orientation_seed;
    recipe.p_one_way = static_cast<float>(spec.asymmetric_params.p_one_way);
    recipe.p_bidirectional =
        static_cast<float>(spec.asymmetric_params.p_bidirectional);
    recipe.gradient_angle_degrees =
        static_cast<float>(spec.asymmetric_params.gradient_angle_degrees);
    recipe.p_against_gradient =
        static_cast<float>(spec.asymmetric_params.p_against_gradient);
    return recipe;
}

[[nodiscard]] RandomAsymmetricParams orientedParams(const ProceduralMazeSpec& spec) {
    RandomAsymmetricParams params = sanitizeRandomAsymmetricParams(spec.asymmetric_params);
    params.seed = spec.orientation_seed;
    return params;
}

[[nodiscard]] const char* passabilityPolicyLabel(
    const MovingAiPassabilityPolicy policy
) noexcept {
    switch (policy) {
        case MovingAiPassabilityPolicy::GroundOnly:
            return "ground_only";
        case MovingAiPassabilityPolicy::GroundAndSwamp:
            return "ground_and_swamp";
        case MovingAiPassabilityPolicy::AllTraversable:
            return "all_traversable";
        case MovingAiPassabilityPolicy::AnyTerrainLetter:
            return "any_terrain_letter";
    }
    return "ground_only";
}

}  // namespace

const char* gridEdgeConversionModeLabel(const GridEdgeConversionMode mode) noexcept {
    switch (mode) {
        case GridEdgeConversionMode::RandomAsymmetric:
            return "random_asymmetric";
        case GridEdgeConversionMode::BidirectionalAll:
            return "bidirectional_all";
        case GridEdgeConversionMode::AcyclicEastSouth:
            return "acyclic_east_south";
        case GridEdgeConversionMode::GradientFlow:
            return "gradient_flow";
    }
    return "random_asymmetric";
}

std::string proceduralMapId(const ProceduralMazeSpec& spec, const uint32_t index) {
    std::ostringstream stream;
    stream << "proc_w" << spec.logical_width << "h" << spec.logical_height
           << "_c" << spec.carve_seed << "_o" << spec.extra_openings
           << "_open" << spec.opening_seed << '_'
           << gridEdgeConversionModeLabel(spec.orientation_mode) << "_i" << index;
    return stream.str();
}

BenchmarkCampaignMapCharacterization characterizeCampaignMap(
    const MazeLayout& layout,
    const DirectedGridGraph& graph,
    const ProceduralMazeSpec& spec,
    const uint64_t gallery_image_hash
) {
    BenchmarkCampaignMapCharacterization characterization{};
    characterization.grid_width = layout.width();
    characterization.grid_height = layout.height();
    characterization.passable_cells = layout.passableCount();
    characterization.num_vertices = graph.numVertices();
    characterization.num_edges = graph.numEdges();
    countPassableUndirectedComponents(
        layout,
        characterization.undirected_component_count,
        characterization.largest_undirected_component
    );

    GraphSearchScratch scratch;
    const KleeneSquaringBoundStats structural =
        kleeneSquaringBoundStatsForCsrGraph(graph.csrGraph(), scratch);
    characterization.num_strongly_connected_components =
        structural.num_strongly_connected_components;
    characterization.largest_strongly_connected_component =
        structural.largest_strongly_connected_component;

    const SccDecomposition decomposition =
        SccDecomposition::compute(graph.csrGraph(), scratch);
    const CondensationGraph condensation =
        CondensationGraph::fromGraph(graph.csrGraph(), decomposition);
    characterization.condensation_vertices = condensation.numComponents();
    characterization.condensation_edges = static_cast<uint32_t>(
        condensation.dag().numEdges()
    );

    characterization.orientation_mode = gridEdgeConversionModeLabel(spec.orientation_mode);
    characterization.carve_seed = spec.carve_seed;
    characterization.opening_seed = spec.opening_seed;
    characterization.extra_openings = spec.extra_openings;
    characterization.p_one_way = static_cast<float>(spec.asymmetric_params.p_one_way);
    characterization.p_bidirectional =
        static_cast<float>(spec.asymmetric_params.p_bidirectional);
    characterization.gradient_angle_degrees = static_cast<float>(
        spec.asymmetric_params.gradient_angle_degrees
    );
    characterization.p_against_gradient =
        static_cast<float>(spec.asymmetric_params.p_against_gradient);
    characterization.orientation_seed = spec.orientation_seed;
    characterization.gallery_image_hash = gallery_image_hash;
    return characterization;
}

bool generateProceduralCampaignMap(
    const ProceduralMazeSpec& spec,
    const uint32_t index,
    const BenchmarkCampaignPaths& paths,
    const std::string& campaign_id,
    GeneratedCampaignMap& generated,
    std::string& error_message
) {
    const test_support::MazeParams maze_params{
        spec.logical_width,
        spec.logical_height,
        spec.carve_seed,
    };
    generated.layout = test_support::generateMazeWithExtraPassages(
        maze_params,
        spec.opening_seed,
        spec.extra_openings
    );
    generated.graph = DirectedGridGraphBuilder::build(
        generated.layout,
        spec.orientation_mode,
        orientedParams(spec)
    );

    generated.map_id = proceduralMapId(spec, index);
    generated.context.map_id = generated.map_id;
    generated.context.generator_type = "procedural_maze";

    const std::filesystem::path gallery_path =
        paths.gallery_dir / (generated.map_id + ".ppm");
    generated.context.gallery_image_path = gallery_path.string();

    if (!writePassabilityPpm(generated.layout, gallery_path, error_message)) {
        return false;
    }
    const uint64_t gallery_hash = hashFileContentsFnv1a(gallery_path);

    const tools::Recipe recipe = recipeFromProceduralSpec(
        spec,
        campaign_id,
        generated.map_id
    );
    const std::filesystem::path recipe_path =
        tools::saveRecipe(paths.recipes_dir, recipe);
    if (recipe_path.empty()) {
        error_message = "Failed to save recipe for " + generated.map_id;
        return false;
    }
    generated.context.recipe_path = recipe_path.string();

    generated.characterization = characterizeCampaignMap(
        generated.layout,
        generated.graph,
        spec,
        gallery_hash
    );
    return true;
}

bool generateProceduralCampaignMaps(
    const BenchmarkCampaignPaths& paths,
    const BenchmarkCampaignMetadata& metadata,
    const ProceduralMazeSpec& spec,
    const uint32_t count,
    std::string& error_message
) {
    for (uint32_t index = 0U; index < count; ++index) {
        GeneratedCampaignMap generated{};
        if (!generateProceduralCampaignMap(
                spec,
                index,
                paths,
                metadata.campaign_id,
                generated,
                error_message)) {
            return false;
        }
        if (!appendBenchmarkCampaignManifestRowFull(
                paths.manifest_csv,
                metadata,
                generated.context,
                generated.characterization,
                error_message)) {
            return false;
        }
    }
    return true;
}

std::filesystem::path defaultMovingAiDatasetsRoot() {
#if defined(HBRICK_SOURCE_DIR)
    return std::filesystem::path(HBRICK_SOURCE_DIR) / "datasets" / "movingai" / "extracted";
#else
    return {};
#endif
}

std::string movingAiCampaignMapId(
    const std::string& set_name,
    const std::string& map_name
) {
    std::string sanitized_set = set_name;
    std::string sanitized_map = map_name;
    for (char& character : sanitized_set) {
        if (character == '/' || character == '\\') {
            character = '_';
        }
    }
    for (char& character : sanitized_map) {
        if (character == '/' || character == '\\') {
            character = '_';
        }
    }
    return "movingai_" + sanitized_set + '_' + sanitized_map;
}

MovingAiCampaignMapSpec movingAiCampaignMapSpecForEntry(
    const std::string& set_name,
    const std::string& map_name,
    const std::filesystem::path& datasets_root
) {
    MovingAiCampaignMapSpec spec{};
    spec.set_name = set_name;
    spec.map_name = map_name;
    spec.datasets_root = datasets_root;
    spec.policy = test_support::passabilityPolicyForMovingAiSet(set_name);
    spec.asymmetric_params = test_support::randomParamsForMovingAiMap(set_name, map_name);
    spec.orientation_seed = spec.asymmetric_params.seed;
    return spec;
}

bool generateMovingAiCampaignMap(
    const MovingAiCampaignMapSpec& spec,
    const BenchmarkCampaignPaths& paths,
    const std::string& /*campaign_id*/,
    GeneratedCampaignMap& generated,
    std::string& error_message
) {
    const std::filesystem::path map_path =
        spec.datasets_root / spec.set_name / "maps" / spec.map_name;
    const MovingAiLoadResult loaded = loadMovingAiMap(map_path);
    if (!loaded.ok()) {
        error_message = "Failed to load MovingAI map: " + map_path.string();
        return false;
    }

    generated.layout = loaded.map.toMazeLayout(spec.policy);
    RandomAsymmetricParams params = sanitizeRandomAsymmetricParams(spec.asymmetric_params);
    params.seed = spec.orientation_seed;
    generated.graph = DirectedGridGraphBuilder::build(
        generated.layout,
        spec.orientation_mode,
        params
    );

    generated.map_id = movingAiCampaignMapId(spec.set_name, spec.map_name);
    generated.context.map_id = generated.map_id;
    generated.context.generator_type = "movingai";

    const std::filesystem::path gallery_path =
        paths.gallery_dir / (generated.map_id + ".ppm");
    generated.context.gallery_image_path = gallery_path.string();
    if (!writePassabilityPpm(generated.layout, gallery_path, error_message)) {
        return false;
    }
    const uint64_t gallery_hash = hashFileContentsFnv1a(gallery_path);

    tools::Recipe recipe{};
    recipe.set_name = spec.set_name;
    recipe.map_name = spec.map_name;
    recipe.policy = spec.policy;
    recipe.mode = spec.orientation_mode;
    recipe.seed = spec.orientation_seed;
    recipe.p_one_way = static_cast<float>(params.p_one_way);
    recipe.p_bidirectional = static_cast<float>(params.p_bidirectional);
    recipe.gradient_angle_degrees = static_cast<float>(params.gradient_angle_degrees);
    recipe.p_against_gradient = static_cast<float>(params.p_against_gradient);

    const std::filesystem::path recipe_path =
        tools::saveRecipe(paths.recipes_dir, recipe);
    if (recipe_path.empty()) {
        error_message = "Failed to save recipe for " + generated.map_id;
        return false;
    }
    generated.context.recipe_path = recipe_path.string();

    ProceduralMazeSpec orientation_spec{};
    orientation_spec.orientation_mode = spec.orientation_mode;
    orientation_spec.orientation_seed = spec.orientation_seed;
    orientation_spec.asymmetric_params = params;
    generated.characterization = characterizeCampaignMap(
        generated.layout,
        generated.graph,
        orientation_spec,
        gallery_hash
    );
    generated.characterization.movingai_set = spec.set_name;
    generated.characterization.movingai_map = spec.map_name;
    generated.characterization.datasets_root = spec.datasets_root.string();
    generated.characterization.passability_policy = passabilityPolicyLabel(spec.policy);
    return true;
}

bool importMovingAiCampaignMaps(
    const BenchmarkCampaignPaths& paths,
    const BenchmarkCampaignMetadata& metadata,
    const std::filesystem::path& datasets_root,
    const std::span<const MovingAiCampaignMapSpec> maps,
    std::string& error_message
) {
    for (const MovingAiCampaignMapSpec& spec : maps) {
        MovingAiCampaignMapSpec resolved = spec;
        if (resolved.datasets_root.empty()) {
            resolved.datasets_root = datasets_root;
        }
        GeneratedCampaignMap generated{};
        if (!generateMovingAiCampaignMap(
                resolved,
                paths,
                metadata.campaign_id,
                generated,
                error_message)) {
            return false;
        }
        if (!appendBenchmarkCampaignManifestRowFull(
                paths.manifest_csv,
                metadata,
                generated.context,
                generated.characterization,
                error_message)) {
            return false;
        }
    }
    return true;
}

std::vector<MovingAiCampaignMapSpec> movingAiSmokeCatalogSpecs(
    const std::filesystem::path& datasets_root
) {
    std::vector<MovingAiCampaignMapSpec> specs;
    const std::vector<test_support::MovingAiMapEntry> catalog =
        test_support::movingAiReachabilityTestCatalog();
    specs.reserve(catalog.size());
    for (const test_support::MovingAiMapEntry& entry : catalog) {
        specs.push_back(movingAiCampaignMapSpecForEntry(
            entry.set_name,
            entry.map_name,
            datasets_root
        ));
    }
    return specs;
}

}  // namespace hbrick
