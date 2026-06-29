#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "hbrick/bench/benchmark_campaign.hpp"
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
    map.generator_type = "unit_test";

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

    const std::string workload = readFile(paths.workload_json);
    EXPECT_NE(workload.find("\"pair_seed\":"), std::string::npos);
    EXPECT_NE(workload.find("\"pair_list_hash\":"), std::string::npos);

    std::filesystem::remove_all(root);
}
