#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <sstream>

#include "hbrick/bench/benchmark_campaign.hpp"
#include "hbrick/bench/benchmark_campaign_dataset.hpp"
#include "hbrick/bench/benchmark_campaign_gallery.hpp"
#include "hbrick/bench/benchmark_campaign_analysis.hpp"
#include "hbrick/bench/benchmark_campaign_config.hpp"
#include "hbrick/bench/benchmark_campaign_run.hpp"
#include "hbrick/bench/reachability_benchmark_util.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

}  // namespace

TEST(BenchmarkCampaign, HashReachabilityQueryPairsIsStable) {
    const std::vector<hbrick::ReachabilityQueryPair> pairs{
        {1U, 2U},
        {3U, 4U},
    };
    const uint64_t first = hbrick::hashReachabilityQueryPairs(pairs);
    const uint64_t second = hbrick::hashReachabilityQueryPairs(pairs);
    EXPECT_EQ(first, second);
    EXPECT_NE(first, 0U);
}

TEST(BenchmarkCampaign, InitializeCreatesLayout) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "hbrick_campaign_init_test";
    std::filesystem::remove_all(root);

    const hbrick::BenchmarkCampaignPaths paths =
        hbrick::benchmarkCampaignPathsFromRoot(root);
    hbrick::BenchmarkCampaignMetadata metadata =
        hbrick::captureBenchmarkCampaignMetadata("test_campaign");
    std::string error;
    ASSERT_TRUE(hbrick::initializeBenchmarkCampaignDirectory(paths, metadata, error))
        << error;

    EXPECT_TRUE(std::filesystem::exists(paths.manifest_csv));
    EXPECT_TRUE(std::filesystem::exists(paths.results_csv));
    EXPECT_TRUE(std::filesystem::exists(paths.metadata_json));
    EXPECT_TRUE(std::filesystem::is_directory(paths.gallery_dir));
    EXPECT_TRUE(std::filesystem::is_directory(paths.recipes_dir));
    EXPECT_TRUE(std::filesystem::is_directory(paths.logs_dir));

    const std::string manifest = readFile(paths.manifest_csv);
    EXPECT_NE(manifest.find("campaign_schema_version"), std::string::npos);

    std::filesystem::remove_all(root);
}

TEST(BenchmarkCampaign, SmokeGridJobWritesResults) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "hbrick_campaign_smoke_test";
    std::filesystem::remove_all(root);

    const hbrick::BenchmarkCampaignPaths paths =
        hbrick::benchmarkCampaignPathsFromRoot(root);
    hbrick::BenchmarkCampaignMetadata metadata =
        hbrick::captureBenchmarkCampaignMetadata("smoke_test");
    std::string error;
    ASSERT_TRUE(hbrick::initializeBenchmarkCampaignDirectory(paths, metadata, error))
        << error;

    hbrick::MazeLayout layout{8U, 8U, true};
    const hbrick::DirectedGridGraph graph =
        hbrick::DirectedGridGraphBuilder::build(
            layout,
            hbrick::GridEdgeConversionMode::RandomAsymmetric,
            hbrick::RandomAsymmetricParams{0x515EEDULL, 0.20L, 0.10L}
        );

    hbrick::BenchmarkCampaignMapContext map{};
    map.map_id = "smoke";
    map.generator_type = "smoke_grid";

    hbrick::ReachabilityBenchmarkConfig config{};
    config.query_count = 16U;
    config.warmup_queries = 4U;
    config.correctness_check_count = 8U;
    config.pair_seed = 0xBEEFULL;
    config.methods = {
        hbrick::ReachabilityBaselineId::CsrBfs,
        hbrick::ReachabilityBaselineId::BrickSearch,
    };

    ASSERT_TRUE(hbrick::runBenchmarkCampaignGridJob(
        paths,
        metadata,
        map,
        graph,
        layout,
        config,
        error
    )) << error;

    const std::string results = readFile(paths.results_csv);
    EXPECT_NE(results.find("CsrBfs"), std::string::npos);
    EXPECT_NE(results.find("BrickSearch"), std::string::npos);
    EXPECT_NE(results.find("correctness_failed"), std::string::npos);
    EXPECT_NE(results.find("map_class"), std::string::npos);
    EXPECT_NE(results.find("steady_clock"), std::string::npos);
    EXPECT_NE(results.find("smoke_grid"), std::string::npos);

    const std::string workload = readFile(paths.workload_json);
    EXPECT_NE(workload.find("\"pair_seed\":"), std::string::npos);
    EXPECT_NE(workload.find("\"pair_list_hash\":"), std::string::npos);

    std::filesystem::remove_all(root);
}

TEST(BenchmarkCampaign, ProceduralMazeGenerationIsDeterministic) {
    hbrick::ProceduralMazeSpec spec{};
    spec.logical_width = 4U;
    spec.logical_height = 4U;
    spec.carve_seed = 11U;
    spec.opening_seed = 22U;
    spec.extra_openings = 2U;
    spec.orientation_seed = 0x515EEDULL;
    spec.asymmetric_params.p_one_way = 0.20L;
    spec.asymmetric_params.p_bidirectional = 0.10L;

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "hbrick_campaign_proc_test";
    std::filesystem::remove_all(root);

    const hbrick::BenchmarkCampaignPaths paths =
        hbrick::benchmarkCampaignPathsFromRoot(root);
    hbrick::BenchmarkCampaignMetadata metadata =
        hbrick::captureBenchmarkCampaignMetadata("proc_test");
    std::string error;
    ASSERT_TRUE(hbrick::initializeBenchmarkCampaignDirectory(paths, metadata, error))
        << error;

    hbrick::GeneratedCampaignMap first{};
    hbrick::GeneratedCampaignMap second{};
    ASSERT_TRUE(hbrick::generateProceduralCampaignMap(
        spec, 0U, paths, metadata.campaign_id, first, error
    )) << error;
    ASSERT_TRUE(hbrick::generateProceduralCampaignMap(
        spec, 0U, paths, metadata.campaign_id, second, error
    )) << error;

    EXPECT_EQ(first.map_id, second.map_id);
    EXPECT_EQ(first.layout.width(), second.layout.width());
    EXPECT_EQ(first.layout.height(), second.layout.height());
    EXPECT_EQ(first.layout.passableCount(), second.layout.passableCount());
    EXPECT_EQ(first.graph.numVertices(), second.graph.numVertices());
    EXPECT_EQ(first.graph.numEdges(), second.graph.numEdges());
    EXPECT_EQ(
        first.characterization.gallery_image_hash,
        second.characterization.gallery_image_hash
    );
    EXPECT_NE(first.characterization.gallery_image_hash, 0U);

    for (uint32_t y = 0U; y < first.layout.height(); ++y) {
        for (uint32_t x = 0U; x < first.layout.width(); ++x) {
            EXPECT_EQ(
                first.layout.isPassable(x, y),
                second.layout.isPassable(x, y)
            );
        }
    }

    std::filesystem::remove_all(root);
}

TEST(BenchmarkCampaign, GalleryPpmHashIsStable) {
    hbrick::MazeLayout layout{5U, 5U, true};
    layout.setPassable(2U, 2U, false);

    const std::filesystem::path image =
        std::filesystem::temp_directory_path() / "hbrick_campaign_gallery.ppm";
    std::string error;
    ASSERT_TRUE(hbrick::writePassabilityPpm(layout, image, error)) << error;

    const uint64_t first = hbrick::hashFileContentsFnv1a(image);
    const uint64_t second = hbrick::hashFileContentsFnv1a(image);
    EXPECT_EQ(first, second);
    EXPECT_NE(first, 0U);

    std::filesystem::remove(image);
}

TEST(BenchmarkCampaign, ParseReachabilityBaselineNames) {
    std::vector<hbrick::ReachabilityBaselineId> methods;
    std::string error;
    ASSERT_TRUE(hbrick::parseReachabilityBaselineCsv(
        "CsrBfs,BrickSearch,HBrick",
        methods,
        error
    )) << error;
    ASSERT_EQ(methods.size(), 3U);
    EXPECT_EQ(methods[0], hbrick::ReachabilityBaselineId::CsrBfs);
    EXPECT_EQ(methods[1], hbrick::ReachabilityBaselineId::BrickSearch);
    EXPECT_EQ(methods[2], hbrick::ReachabilityBaselineId::HBrick);

    hbrick::ReachabilityBaselineId method = hbrick::ReachabilityBaselineId::CsrBfs;
    EXPECT_TRUE(hbrick::reachabilityBaselineFromName("brickclosure", method));
    EXPECT_EQ(method, hbrick::ReachabilityBaselineId::BrickClosure);
}

TEST(BenchmarkCampaign, ConfigSweepExpandsBrickVariants) {
    const hbrick::ReachabilityBenchmarkConfig base =
        hbrick::benchmarkCampaignConfigFromPreset("brick");
    std::string error;
    const std::vector<hbrick::ReachabilityBenchmarkConfig> expanded =
        hbrick::expandBenchmarkCampaignConfigSweep(base, "brick", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(expanded.size(), 4U);
}

TEST(BenchmarkCampaign, ConfigIdIsStable) {
    hbrick::ReachabilityBenchmarkConfig config =
        hbrick::benchmarkCampaignConfigFromPreset("smoke");
    const std::string first = hbrick::benchmarkCampaignConfigId(config);
    const std::string second = hbrick::benchmarkCampaignConfigId(config);
    EXPECT_EQ(first, second);
    EXPECT_FALSE(first.empty());
}

TEST(BenchmarkCampaign, ResultsCsvHeaderIsGolden) {
    const char* header = hbrick::benchmarkCampaignResultsCsvHeaderLine();
    EXPECT_NE(std::strstr(header, "query_min_ns"), nullptr);
    EXPECT_NE(std::strstr(header, "query_max_ns"), nullptr);
    EXPECT_NE(std::strstr(header, "correctness_failed"), nullptr);
    EXPECT_NE(std::strstr(header, "map_class"), nullptr);
    EXPECT_NE(std::strstr(header, "kleene_rounds_scheduled"), nullptr);
    EXPECT_NE(std::strstr(header, "timer_source"), nullptr);
    EXPECT_EQ(header[std::strlen(header) - 1U], '\n');
}

TEST(BenchmarkCampaign, MapClassDerivation) {
    hbrick::BenchmarkCampaignMapCharacterization stats{};
    EXPECT_EQ(
        hbrick::benchmarkCampaignMapClass("procedural_maze", stats),
        "perfect_maze"
    );
    stats.extra_openings = 2U;
    EXPECT_EQ(
        hbrick::benchmarkCampaignMapClass("procedural_maze", stats),
        "cyclic_maze"
    );
    EXPECT_EQ(hbrick::benchmarkCampaignMapClass("movingai", stats), "movingai");
    EXPECT_EQ(hbrick::benchmarkCampaignMapClass("smoke_grid", stats), "smoke_grid");
}

TEST(BenchmarkCampaign, KleeneOraclePresetIncludesClosureOracles) {
    const hbrick::ReachabilityBenchmarkConfig config =
        hbrick::benchmarkCampaignConfigFromPreset("kleene-oracle");
    ASSERT_EQ(config.methods.size(), 3U);
    EXPECT_EQ(config.methods[0], hbrick::ReachabilityBaselineId::FullClosure);
    EXPECT_EQ(config.methods[1], hbrick::ReachabilityBaselineId::SccDagClosure);
    EXPECT_EQ(config.methods[2], hbrick::ReachabilityBaselineId::BrickClosure);
}

TEST(BenchmarkCampaign, GenerateAndRunFromManifest) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "hbrick_campaign_generate_run";
    std::filesystem::remove_all(root);

    const hbrick::BenchmarkCampaignPaths paths =
        hbrick::benchmarkCampaignPathsFromRoot(root);
    hbrick::BenchmarkCampaignMetadata metadata =
        hbrick::captureBenchmarkCampaignMetadata("generate_run");
    std::string error;
    ASSERT_TRUE(hbrick::initializeBenchmarkCampaignDirectory(paths, metadata, error))
        << error;

    hbrick::ProceduralMazeSpec spec{};
    spec.logical_width = 3U;
    spec.logical_height = 3U;
    spec.carve_seed = 7U;
    spec.opening_seed = 8U;
    spec.extra_openings = 1U;
    spec.orientation_seed = 0xBEEFULL;

    ASSERT_TRUE(hbrick::generateProceduralCampaignMaps(
        paths, metadata, spec, 1U, error
    )) << error;

    const std::string manifest = readFile(paths.manifest_csv);
    EXPECT_NE(manifest.find("procedural_maze"), std::string::npos);
    EXPECT_NE(manifest.find("gallery_image_hash"), std::string::npos);

    hbrick::ReachabilityBenchmarkConfig config{};
    config.query_count = 8U;
    config.warmup_queries = 2U;
    config.correctness_check_count = 4U;
    config.pair_seed = 0x1234ULL;
    config.methods = {hbrick::ReachabilityBaselineId::CsrBfs};

    hbrick::BenchmarkCampaignRunOptions options{};
    ASSERT_TRUE(hbrick::runBenchmarkCampaignFromManifest(
        paths, metadata, config, options, error
    )) << error;

    const std::string results = readFile(paths.results_csv);
    EXPECT_NE(results.find("CsrBfs"), std::string::npos);

    std::filesystem::remove_all(root);
}
