/**
 * @file benchmark_campaign_main.cpp
 * @brief CLI driver for headless benchmark campaigns.
 */

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "hbrick/bench/benchmark_campaign.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace {

void printUsage(const char* program) {
    std::fprintf(
        stderr,
        "Usage:\n"
        "  %s init <campaign_dir> [--id NAME]\n"
        "  %s smoke <campaign_dir> [--id NAME]\n"
        "\n"
        "  init   Create campaign layout (manifest, results headers, metadata).\n"
        "  smoke  init + run a small grid benchmark (CsrBfs, BrickSearch, BrickClosure, HBrick).\n",
        program,
        program
    );
}

[[nodiscard]] std::string readOptionValue(
    const int argc,
    char** argv,
    const int index
) {
    if (index + 1 >= argc) {
        return {};
    }
    return argv[index + 1];
}

[[nodiscard]] std::string campaignIdFromArgs(const int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--id") == 0) {
            return readOptionValue(argc, argv, index);
        }
    }
    return "campaign";
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
    hbrick::ReachabilityBenchmarkConfig config{};
    config.query_count = 64U;
    config.warmup_queries = 8U;
    config.correctness_check_count = 16U;
    config.pair_seed = 0xCA0151ULL;
    config.max_memory_bytes = 512ULL << 20;
    config.methods = {
        hbrick::ReachabilityBaselineId::CsrBfs,
        hbrick::ReachabilityBaselineId::BrickSearch,
        hbrick::ReachabilityBaselineId::BrickClosure,
        hbrick::ReachabilityBaselineId::HBrick,
    };
    return config;
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

}  // namespace

int main(const int argc, char** argv) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    const char* command = argv[1];
    const std::filesystem::path campaign_dir = argv[2];
    const std::string campaign_id = campaignIdFromArgs(argc, argv);

    if (std::strcmp(command, "init") == 0) {
        return runInit(campaign_dir, campaign_id);
    }
    if (std::strcmp(command, "smoke") == 0) {
        return runSmoke(campaign_dir, campaign_id);
    }

    printUsage(argv[0]);
    return 1;
}
