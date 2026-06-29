/**
 * @file benchmark_campaign_main.cpp
 * @brief CLI driver for headless benchmark campaigns.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "hbrick/bench/benchmark_campaign.hpp"
#include "hbrick/bench/benchmark_campaign_config.hpp"
#include "hbrick/bench/benchmark_campaign_dataset.hpp"
#include "hbrick/bench/benchmark_campaign_log.hpp"
#include "hbrick/bench/benchmark_campaign_run.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace {

void printUsage(const char* program) {
    std::fprintf(
        stderr,
        "Usage:\n"
        "  %s init <campaign_dir> [--id NAME]\n"
        "  %s generate <campaign_dir> [--id NAME] [--count N]\n"
        "       [--logical-width W] [--logical-height H]\n"
        "       [--carve-seed S] [--opening-seed S] [--extra-openings N]\n"
        "       [--orientation-seed S] [--p-one-way P] [--p-bidirectional P]\n"
        "  %s import-movingai <campaign_dir> [--id NAME]\n"
        "       [--datasets-root PATH] [--movingai-set SET --movingai-map MAP]\n"
        "       [--movingai-smoke]\n"
        "  %s run <campaign_dir> [--id NAME] [--resume] [--map MAP_ID ...]\n"
        "       [--preset smoke|csr|brick|kleene-oracle|all] [--method NAME ...]\n"
        "       [--methods CsrBfs,BrickSearch,...] [--configs default|brick|hbrick|all]\n"
        "       [--datasets-root PATH]\n"
        "  %s smoke <campaign_dir> [--id NAME]\n"
        "  %s kleene-micro\n"
        "\n"
        "  init            Create campaign layout (manifest, results headers, metadata).\n"
        "  generate        Procedural mazes + recipes + gallery PPM + manifest rows.\n"
        "  import-movingai MovingAI maps from disk into manifest (no benchmarks).\n"
        "  run             Benchmark manifest rows (use --resume to skip completed maps).\n"
        "  smoke           init + run a small grid benchmark.\n"
        "  kleene-micro    Run kleene_closure_benchmark (preprocess-only Kleene/Warshall study).\n",
        program,
        program,
        program,
        program,
        program,
        program
    );
}

[[nodiscard]] std::string readOptionValue(const int argc, char** argv, const int index) {
    if (index + 1 >= argc) {
        return {};
    }
    return argv[index + 1];
}

[[nodiscard]] bool hasOption(const int argc, char** argv, const char* name) {
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], name) == 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string campaignIdFromArgs(const int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--id") == 0) {
            return readOptionValue(argc, argv, index);
        }
    }
    return "campaign";
}

[[nodiscard]] uint64_t u64Option(
    const int argc,
    char** argv,
    const char* name,
    const uint64_t default_value
) {
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], name) == 0) {
            const std::string value = readOptionValue(argc, argv, index);
            if (!value.empty()) {
                return std::strtoull(value.c_str(), nullptr, 0);
            }
        }
    }
    return default_value;
}

[[nodiscard]] uint32_t u32Option(
    const int argc,
    char** argv,
    const char* name,
    const uint32_t default_value
) {
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], name) == 0) {
            const std::string value = readOptionValue(argc, argv, index);
            if (!value.empty()) {
                return static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 0));
            }
        }
    }
    return default_value;
}

[[nodiscard]] long double longDoubleOption(
    const int argc,
    char** argv,
    const char* name,
    const long double default_value
) {
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], name) == 0) {
            const std::string value = readOptionValue(argc, argv, index);
            if (!value.empty()) {
                return std::strtold(value.c_str(), nullptr);
            }
        }
    }
    return default_value;
}

[[nodiscard]] std::vector<std::string> mapIdsFromArgs(const int argc, char** argv) {
    std::vector<std::string> map_ids;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--map") == 0) {
            const std::string value = readOptionValue(argc, argv, index);
            if (!value.empty()) {
                map_ids.push_back(value);
            }
        }
    }
    return map_ids;
}

[[nodiscard]] std::string stringOption(
    const int argc,
    char** argv,
    const char* name,
    const std::string& default_value
) {
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], name) == 0) {
            const std::string value = readOptionValue(argc, argv, index);
            if (!value.empty()) {
                return value;
            }
        }
    }
    return default_value;
}

[[nodiscard]] std::vector<std::string> methodNamesFromArgs(const int argc, char** argv) {
    std::vector<std::string> names;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--method") == 0) {
            const std::string value = readOptionValue(argc, argv, index);
            if (!value.empty()) {
                names.push_back(value);
            }
        }
    }
    const std::string csv = stringOption(argc, argv, "--methods", "");
    if (!csv.empty()) {
        std::string token;
        for (char character : csv) {
            if (character == ',') {
                if (!token.empty()) {
                    names.push_back(token);
                    token.clear();
                }
            } else {
                token.push_back(character);
            }
        }
        if (!token.empty()) {
            names.push_back(token);
        }
    }
    return names;
}

[[nodiscard]] bool ensureCampaignInitialized(
    const hbrick::BenchmarkCampaignPaths& paths,
    hbrick::BenchmarkCampaignMetadata& metadata,
    std::string& error
) {
    if (std::filesystem::exists(paths.manifest_csv)) {
        return true;
    }
    return hbrick::initializeBenchmarkCampaignDirectory(paths, metadata, error);
}

[[nodiscard]] hbrick::ReachabilityBenchmarkConfig benchmarkConfigFromArgs(
    const int argc,
    char** argv,
    std::string& error
) {
    const std::string preset = stringOption(argc, argv, "--preset", "all");
    hbrick::ReachabilityBenchmarkConfig config =
        hbrick::benchmarkCampaignConfigFromPreset(preset.c_str());

    const std::vector<std::string> method_names = methodNamesFromArgs(argc, argv);
    if (!method_names.empty()) {
        if (!hbrick::parseReachabilityBaselineNames(
                method_names,
                config.methods,
                error)) {
            config.methods.clear();
        }
    }
    return config;
}

[[nodiscard]] hbrick::DirectedGridGraph buildSmokeGridGraph(hbrick::MazeLayout& layout) {
    layout = hbrick::MazeLayout{8U, 8U, true};
    return hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0xCA0151ULL, 0.25L, 0.15L, 45.0, 0.05L}
    );
}

[[nodiscard]] hbrick::ReachabilityBenchmarkConfig smokeBenchmarkConfig() {
    return hbrick::benchmarkCampaignConfigFromPreset("smoke");
}

[[nodiscard]] hbrick::ProceduralMazeSpec proceduralSpecFromArgs(const int argc, char** argv) {
    hbrick::ProceduralMazeSpec spec{};
    spec.logical_width = u32Option(argc, argv, "--logical-width", 5U);
    spec.logical_height = u32Option(argc, argv, "--logical-height", 5U);
    spec.carve_seed = u64Option(argc, argv, "--carve-seed", 1U);
    spec.opening_seed = u64Option(argc, argv, "--opening-seed", 2U);
    spec.extra_openings = u32Option(argc, argv, "--extra-openings", 3U);
    spec.orientation_seed = u64Option(argc, argv, "--orientation-seed", 0xCA0151ULL);
    spec.asymmetric_params.p_one_way =
        longDoubleOption(argc, argv, "--p-one-way", 0.25L);
    spec.asymmetric_params.p_bidirectional =
        longDoubleOption(argc, argv, "--p-bidirectional", 0.15L);
    spec.asymmetric_params.gradient_angle_degrees = 45.0;
    spec.asymmetric_params.p_against_gradient = 0.05L;
    return spec;
}

int runInit(const std::filesystem::path& campaign_dir, std::string campaign_id) {
    const hbrick::BenchmarkCampaignPaths paths =
        hbrick::benchmarkCampaignPathsFromRoot(campaign_dir);
    hbrick::BenchmarkCampaignMetadata metadata =
        hbrick::captureBenchmarkCampaignMetadata(std::move(campaign_id));
    std::string error;
    if (!hbrick::initializeBenchmarkCampaignDirectory(paths, metadata, error)) {
        std::fprintf(stderr, "init failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("Campaign initialized at %s\n", campaign_dir.c_str());
    return 0;
}

int runImportMovingAi(
    const std::filesystem::path& campaign_dir,
    std::string campaign_id,
    const int argc,
    char** argv
) {
    const hbrick::BenchmarkCampaignPaths paths =
        hbrick::benchmarkCampaignPathsFromRoot(campaign_dir);
    hbrick::BenchmarkCampaignMetadata metadata =
        hbrick::captureBenchmarkCampaignMetadata(campaign_id);

    std::string error;
    if (!ensureCampaignInitialized(paths, metadata, error)) {
        std::fprintf(stderr, "import-movingai init failed: %s\n", error.c_str());
        return 1;
    }

    std::filesystem::path datasets_root =
        stringOption(argc, argv, "--datasets-root", "");
    if (datasets_root.empty()) {
        datasets_root = hbrick::defaultMovingAiDatasetsRoot();
    }
    if (datasets_root.empty() || !std::filesystem::is_directory(datasets_root)) {
        std::fprintf(
            stderr,
            "import-movingai failed: datasets root not found (use --datasets-root)\n"
        );
        return 1;
    }

    std::vector<hbrick::MovingAiCampaignMapSpec> specs;
    if (hasOption(argc, argv, "--movingai-smoke")) {
        specs = hbrick::movingAiSmokeCatalogSpecs(datasets_root);
    } else {
        const std::string set_name = stringOption(argc, argv, "--movingai-set", "");
        const std::string map_name = stringOption(argc, argv, "--movingai-map", "");
        if (set_name.empty() || map_name.empty()) {
            std::fprintf(
                stderr,
                "import-movingai requires --movingai-set and --movingai-map "
                "or --movingai-smoke\n"
            );
            return 1;
        }
        specs.push_back(hbrick::movingAiCampaignMapSpecForEntry(
            set_name,
            map_name,
            datasets_root
        ));
    }

    if (specs.empty()) {
        std::fprintf(stderr, "import-movingai failed: no maps matched\n");
        return 1;
    }

    if (!hbrick::importMovingAiCampaignMaps(
            paths,
            metadata,
            datasets_root,
            specs,
            error)) {
        std::fprintf(stderr, "import-movingai failed: %s\n", error.c_str());
        return 1;
    }

    std::printf(
        "Imported %zu MovingAI map(s). Manifest: %s\n",
        specs.size(),
        paths.manifest_csv.c_str()
    );
    return 0;
}

int runGenerate(
    const std::filesystem::path& campaign_dir,
    std::string campaign_id,
    const int argc,
    char** argv
) {
    const hbrick::BenchmarkCampaignPaths paths =
        hbrick::benchmarkCampaignPathsFromRoot(campaign_dir);
    hbrick::BenchmarkCampaignMetadata metadata =
        hbrick::captureBenchmarkCampaignMetadata(campaign_id);

    std::string error;
    if (!ensureCampaignInitialized(paths, metadata, error)) {
        std::fprintf(stderr, "generate init failed: %s\n", error.c_str());
        return 1;
    }

    if (hasOption(argc, argv, "--movingai-set")
        || hasOption(argc, argv, "--movingai-smoke")) {
        return runImportMovingAi(campaign_dir, campaign_id, argc, argv);
    }

    const hbrick::ProceduralMazeSpec spec = proceduralSpecFromArgs(argc, argv);
    const uint32_t count = u32Option(argc, argv, "--count", 1U);
    if (!hbrick::generateProceduralCampaignMaps(
            paths,
            metadata,
            spec,
            count,
            error)) {
        std::fprintf(stderr, "generate failed: %s\n", error.c_str());
        return 1;
    }

    std::printf(
        "Generated %u procedural map(s). Manifest: %s\n",
        count,
        paths.manifest_csv.c_str()
    );
    return 0;
}

int runBenchmark(
    const std::filesystem::path& campaign_dir,
    std::string campaign_id,
    const int argc,
    char** argv
) {
    const hbrick::BenchmarkCampaignPaths paths =
        hbrick::benchmarkCampaignPathsFromRoot(campaign_dir);
    hbrick::BenchmarkCampaignMetadata metadata =
        hbrick::captureBenchmarkCampaignMetadata(campaign_id);

    hbrick::BenchmarkCampaignRunOptions options{};
    options.resume = hasOption(argc, argv, "--resume");
    options.map_ids = mapIdsFromArgs(argc, argv);
    const std::string datasets_root = stringOption(argc, argv, "--datasets-root", "");
    if (!datasets_root.empty()) {
        options.datasets_root_override = datasets_root;
    }

    std::string error;
    const std::string config_sweeps_csv = stringOption(argc, argv, "--configs", "default");
    if (!hbrick::parseBenchmarkCampaignConfigSweepNames(
            config_sweeps_csv,
            options.config_sweeps,
            error)) {
        std::fprintf(stderr, "run failed: %s\n", error.c_str());
        return 1;
    }

    hbrick::BenchmarkCampaignLogger logger;
    if (!logger.open(paths.logs_dir, campaign_id, error)) {
        std::fprintf(stderr, "run warning: could not open log file: %s\n", error.c_str());
    } else {
        options.logger = &logger;
        std::printf("Log file: %s\n", logger.logPath().c_str());
    }

    hbrick::ReachabilityBenchmarkConfig config = benchmarkConfigFromArgs(argc, argv, error);
    if (!error.empty()) {
        std::fprintf(stderr, "run failed: %s\n", error.c_str());
        return 1;
    }

    if (!hbrick::runBenchmarkCampaignFromManifest(
            paths,
            metadata,
            config,
            options,
            error)) {
        std::fprintf(stderr, "run failed: %s\n", error.c_str());
        return 1;
    }

    std::printf("Benchmark run complete. Results: %s\n", paths.results_csv.c_str());
    return 0;
}

int runSmoke(const std::filesystem::path& campaign_dir, std::string campaign_id) {
    if (runInit(campaign_dir, campaign_id) != 0) {
        return 1;
    }

    const hbrick::BenchmarkCampaignPaths paths =
        hbrick::benchmarkCampaignPathsFromRoot(campaign_dir);
    hbrick::BenchmarkCampaignMetadata metadata =
        hbrick::captureBenchmarkCampaignMetadata(campaign_id);

    hbrick::MazeLayout layout{8U, 8U, true};
    const hbrick::DirectedGridGraph graph = buildSmokeGridGraph(layout);

    hbrick::BenchmarkCampaignMapContext map{};
    map.map_id = "smoke_8x8_random_asymmetric";
    map.generator_type = "smoke_grid";
    map.gallery_image_path = (paths.gallery_dir / "smoke_8x8.ppm").string();

    std::string error;
    if (!hbrick::runBenchmarkCampaignGridJob(
            paths,
            metadata,
            map,
            graph,
            layout,
            smokeBenchmarkConfig(),
            error)) {
        std::fprintf(stderr, "smoke benchmark failed: %s\n", error.c_str());
        return 1;
    }

    std::printf(
        "Smoke benchmark complete. Results: %s\n",
        paths.results_csv.c_str()
    );
    return 0;
}

int runKleeneMicro(const char* program_path) {
    const std::filesystem::path executable_path = program_path;
    const std::filesystem::path kleene_binary =
        executable_path.parent_path() / "kleene_closure_benchmark";
    std::error_code error;
    if (!std::filesystem::exists(kleene_binary, error)) {
        std::fprintf(
            stderr,
            "kleene-micro: %s not found (build kleene_closure_benchmark)\n",
            kleene_binary.c_str()
        );
        return 1;
    }
    const std::string command = kleene_binary.string();
    std::printf("Running %s\n", command.c_str());
    return std::system(command.c_str());
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    const char* command = argv[1];
    if (std::strcmp(command, "kleene-micro") == 0) {
        return runKleeneMicro(argv[0]);
    }

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    const std::filesystem::path campaign_dir = argv[2];
    const std::string campaign_id = campaignIdFromArgs(argc, argv);

    if (std::strcmp(command, "init") == 0) {
        return runInit(campaign_dir, campaign_id);
    }
    if (std::strcmp(command, "import-movingai") == 0) {
        return runImportMovingAi(campaign_dir, campaign_id, argc, argv);
    }
    if (std::strcmp(command, "generate") == 0) {
        return runGenerate(campaign_dir, campaign_id, argc, argv);
    }
    if (std::strcmp(command, "run") == 0) {
        return runBenchmark(campaign_dir, campaign_id, argc, argv);
    }
    if (std::strcmp(command, "smoke") == 0) {
        return runSmoke(campaign_dir, campaign_id);
    }

    printUsage(argv[0]);
    return 1;
}
