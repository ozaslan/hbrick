#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "hbrick/bench/benchmark_campaign.hpp"
#include "hbrick/bench/benchmark_campaign_dataset.hpp"
#include "hbrick/bench/benchmark_campaign_gallery.hpp"
#include "hbrick/bench/benchmark_campaign_analysis.hpp"
#include "hbrick/bench/benchmark_campaign_config.hpp"
#include "hbrick/bench/benchmark_campaign_run.hpp"
#include "hbrick/tile/hbrick_config.hpp"
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
    EXPECT_TRUE(std::filesystem::is_directory(paths.workloads_dir));

    const std::string manifest = readFile(paths.manifest_csv);
    EXPECT_NE(manifest.find("campaign_schema_version"), std::string::npos);

    // Second init must not wipe an existing campaign.
    {
        std::ofstream marker(paths.results_csv, std::ios::app);
        marker << "keep-me\n";
    }
    ASSERT_TRUE(hbrick::initializeBenchmarkCampaignDirectory(paths, metadata, error))
        << error;
    EXPECT_NE(readFile(paths.results_csv).find("keep-me"), std::string::npos);

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

TEST(BenchmarkCampaign, OpenGridGenerationIsDeterministicAndRebuildable) {
    hbrick::ProceduralMazeSpec spec{};
    spec.all_passable = true;
    spec.logical_width = 16U;
    spec.logical_height = 16U;
    spec.orientation_seed = 0x48425601ULL;
    spec.asymmetric_params.p_one_way = 0.05L;
    spec.asymmetric_params.p_bidirectional = 0.00L;

    EXPECT_EQ(
        hbrick::proceduralMapId(spec, 0U),
        "open_w16h16_ow05_bi00_s1212306945_random_asymmetric_i0"
    );

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "hbrick_campaign_open_grid_test";
    std::filesystem::remove_all(root);

    const hbrick::BenchmarkCampaignPaths paths =
        hbrick::benchmarkCampaignPathsFromRoot(root);
    hbrick::BenchmarkCampaignMetadata metadata =
        hbrick::captureBenchmarkCampaignMetadata("open_grid_test");
    std::string error;
    ASSERT_TRUE(hbrick::initializeBenchmarkCampaignDirectory(paths, metadata, error))
        << error;

    hbrick::GeneratedCampaignMap generated{};
    ASSERT_TRUE(hbrick::generateProceduralCampaignMap(
        spec, 0U, paths, metadata.campaign_id, generated, error
    )) << error;

    EXPECT_EQ(generated.context.generator_type, "open_grid");
    EXPECT_EQ(generated.layout.width(), 16U);
    EXPECT_EQ(generated.layout.height(), 16U);
    EXPECT_EQ(generated.layout.passableCount(), 256U);
    EXPECT_EQ(generated.characterization.grid_width, 16U);
    EXPECT_EQ(generated.characterization.grid_height, 16U);
    EXPECT_EQ(generated.characterization.passable_cells, 256U);
    EXPECT_EQ(
        hbrick::benchmarkCampaignMapClass(
            generated.context.generator_type,
            generated.characterization
        ),
        "open_grid"
    );

    hbrick::BenchmarkCampaignManifestEntry entry{};
    entry.map = generated.context;
    entry.characterization = generated.characterization;

    hbrick::MazeLayout rebuilt_layout{1U, 1U, false};
    hbrick::DirectedGridGraph rebuilt_graph{};
    ASSERT_TRUE(hbrick::rebuildCampaignMapFromManifestEntry(
        entry,
        {},
        rebuilt_layout,
        rebuilt_graph,
        error
    )) << error;
    EXPECT_EQ(rebuilt_layout.width(), 16U);
    EXPECT_EQ(rebuilt_layout.passableCount(), 256U);
    EXPECT_EQ(rebuilt_graph.numVertices(), generated.graph.numVertices());
    EXPECT_EQ(rebuilt_graph.numEdges(), generated.graph.numEdges());

    std::filesystem::remove_all(root);
}

TEST(BenchmarkCampaign, OpenGridFivePercentOneWayKeepsNeighborArcs) {
    hbrick::ProceduralMazeSpec spec{};
    spec.all_passable = true;
    spec.logical_width = 32U;
    spec.logical_height = 32U;
    spec.orientation_seed = 0x48425601ULL;
    spec.asymmetric_params.p_one_way = 0.05L;
    spec.asymmetric_params.p_bidirectional = 0.00L;
    spec.asymmetric_params.p_edge_drop = 0.00L;

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "hbrick_campaign_open_grid_ow05";
    std::filesystem::remove_all(root);

    const hbrick::BenchmarkCampaignPaths paths =
        hbrick::benchmarkCampaignPathsFromRoot(root);
    hbrick::BenchmarkCampaignMetadata metadata =
        hbrick::captureBenchmarkCampaignMetadata("open_grid_ow05");
    std::string error;
    ASSERT_TRUE(hbrick::initializeBenchmarkCampaignDirectory(paths, metadata, error))
        << error;

    hbrick::GeneratedCampaignMap generated{};
    ASSERT_TRUE(hbrick::generateProceduralCampaignMap(
        spec, 0U, paths, metadata.campaign_id, generated, error
    )) << error;

    EXPECT_EQ(
        generated.map_id,
        "open_w32h32_ow05_bi95_s1212306945_random_asymmetric_i0"
    );
    EXPECT_NEAR(generated.characterization.p_one_way, 0.05F, 1.0e-5F);
    EXPECT_NEAR(generated.characterization.p_bidirectional, 0.95F, 1.0e-5F);

    const uint64_t pairs = generated.layout.passableAdjacentPairCount();
    const double ratio =
        static_cast<double>(generated.graph.numEdges()) / static_cast<double>(pairs);
    EXPECT_GT(ratio, 1.85);
    EXPECT_LT(ratio, 2.0);

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

    EXPECT_TRUE(hbrick::reachabilityBaselineFromName("FullClosureKleene", method));
    EXPECT_EQ(method, hbrick::ReachabilityBaselineId::FullClosureKleene);
    EXPECT_STREQ(
        hbrick::reachabilityBaselineName(hbrick::ReachabilityBaselineId::FullClosureKleene),
        "FullClosureKleene"
    );

    EXPECT_TRUE(hbrick::reachabilityBaselineFromName("warshall", method));
    EXPECT_EQ(method, hbrick::ReachabilityBaselineId::FullClosure);

    EXPECT_TRUE(hbrick::reachabilityBaselineFromName("hbrick_skip_lift", method));
    EXPECT_EQ(method, hbrick::ReachabilityBaselineId::HBrickSkipLift);
    EXPECT_STREQ(
        hbrick::reachabilityBaselineName(hbrick::ReachabilityBaselineId::HBrickSkipLift),
        "HBrickSkipLift"
    );
}

TEST(BenchmarkCampaign, AllPresetIncludesHbrickVariants) {
    const hbrick::ReachabilityBenchmarkConfig config =
        hbrick::benchmarkCampaignConfigFromPreset("all");
    const auto has = [&](hbrick::ReachabilityBaselineId id) {
        return std::find(config.methods.begin(), config.methods.end(), id)
            != config.methods.end();
    };
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::HBrick));
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::HBrickMicroBfs));
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::HBrickFusedLift));
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::HBrickFusedLiftCache));
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::HBrickSkipLift));
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::HBrickSccLabel));
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::BrickSearch));
}

TEST(BenchmarkCampaign, ConfigSweepExpandsBrickVariants) {
    const hbrick::ReachabilityBenchmarkConfig base =
        hbrick::benchmarkCampaignConfigFromPreset("brick");
    std::string error;
    const std::vector<hbrick::ReachabilityBenchmarkConfig> brick =
        hbrick::expandBenchmarkCampaignConfigSweep(base, "brick", error);
    ASSERT_TRUE(error.empty()) << error;
    // 5 tiles × 2 Kleene modes
    EXPECT_EQ(brick.size(), 10U);

    error.clear();
    const std::vector<hbrick::ReachabilityBenchmarkConfig> tiles =
        hbrick::expandBenchmarkCampaignConfigSweep(base, "brick-tile", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(tiles.size(), 5U);

    error.clear();
    const std::vector<hbrick::ReachabilityBenchmarkConfig> hbrick =
        hbrick::expandBenchmarkCampaignConfigSweep(base, "hbrick", error);
    ASSERT_TRUE(error.empty()) << error;
    // 3 tiles × depth {2, full} (no 4×4 / 8×8)
    EXPECT_EQ(hbrick.size(), 6U);

    error.clear();
    const std::vector<hbrick::ReachabilityBenchmarkConfig> paper =
        hbrick::expandBenchmarkCampaignConfigSweep(base, "paper", error);
    ASSERT_TRUE(error.empty()) << error;
    // 1 generic + 3 BRICK tiles + 12 H-BRICK (3 tiles × 4 depths)
    EXPECT_EQ(paper.size(), 16U);

    uint32_t full_closure_configs = 0U;
    uint32_t generic_configs = 0U;
    uint32_t brick_configs = 0U;
    uint32_t hbrick_configs = 0U;
    std::set<std::string> config_ids;
    for (const hbrick::ReachabilityBenchmarkConfig& variant : paper) {
        config_ids.insert(hbrick::benchmarkCampaignConfigId(variant));
        const bool has_full_closure =
            std::find(
                variant.methods.begin(),
                variant.methods.end(),
                hbrick::ReachabilityBaselineId::FullClosure
            ) != variant.methods.end();
        if (has_full_closure) {
            ++full_closure_configs;
        }
        const bool has_hbrick =
            std::find(
                variant.methods.begin(),
                variant.methods.end(),
                hbrick::ReachabilityBaselineId::HBrick
            ) != variant.methods.end();
        const bool has_brick =
            std::find(
                variant.methods.begin(),
                variant.methods.end(),
                hbrick::ReachabilityBaselineId::BrickSearch
            ) != variant.methods.end();
        if (has_hbrick) {
            ++hbrick_configs;
            EXPECT_EQ(variant.methods.size(), 1U);
        } else if (has_brick) {
            ++brick_configs;
            EXPECT_FALSE(has_full_closure);
        } else {
            ++generic_configs;
            EXPECT_TRUE(has_full_closure);
        }
    }
    EXPECT_EQ(config_ids.size(), 16U);
    EXPECT_EQ(full_closure_configs, 1U);
    EXPECT_EQ(generic_configs, 1U);
    EXPECT_EQ(brick_configs, 3U);
    EXPECT_EQ(hbrick_configs, 12U);
}

TEST(BenchmarkCampaign, ManuscriptPresetMatchesPublishedProtocol) {
    const hbrick::ReachabilityBenchmarkConfig config =
        hbrick::benchmarkCampaignConfigFromPreset("manuscript");
    EXPECT_EQ(config.query_count, 8192U);
    EXPECT_EQ(config.warmup_queries, 128U);
    EXPECT_EQ(config.query_timing_chunks, 128U);
    EXPECT_EQ(config.brick_tile_size.width, 24U);
    EXPECT_EQ(config.hbrick_group_size.group_w, 4U);
    EXPECT_EQ(config.hbrick_max_depth, hbrick::kHBrickFullDepth);
    EXPECT_TRUE(config.isolate_methods);
    const auto has = [&](hbrick::ReachabilityBaselineId id) {
        return std::find(config.methods.begin(), config.methods.end(), id)
            != config.methods.end();
    };
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::HBrickSkipLift));
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::CsrBfs));
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::SccDagSearch));
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::Grail));
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::Oreach));
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::TwoHop));
    EXPECT_TRUE(has(hbrick::ReachabilityBaselineId::BrickSearch));

    std::string error;
    const std::vector<hbrick::ReachabilityBenchmarkConfig> sweep =
        hbrick::expandBenchmarkCampaignConfigSweep(config, "manuscript", error);
    ASSERT_TRUE(error.empty()) << error;
    // 8 tiles × 3 groups, plus 8 flat BrickSearch tiles.
    EXPECT_EQ(sweep.size(), 32U);

    uint32_t skip_lift_cells = 0U;
    uint32_t flat_cells = 0U;
    bool operating_point_has_baselines = false;
    for (const hbrick::ReachabilityBenchmarkConfig& variant : sweep) {
        const bool flat = variant.hbrick_group_size.group_w == 0U;
        const bool skip =
            std::find(
                variant.methods.begin(),
                variant.methods.end(),
                hbrick::ReachabilityBaselineId::HBrickSkipLift
            ) != variant.methods.end();
        if (flat) {
            ++flat_cells;
            EXPECT_EQ(variant.methods.size(), 1U);
        } else if (skip) {
            ++skip_lift_cells;
            if (variant.brick_tile_size.width == 24U
                && variant.hbrick_group_size.group_w == 4U) {
                operating_point_has_baselines = variant.methods.size() == 6U;
            } else {
                EXPECT_EQ(variant.methods.size(), 1U);
            }
        }
    }
    EXPECT_EQ(skip_lift_cells, 24U);
    EXPECT_EQ(flat_cells, 8U);
    EXPECT_TRUE(operating_point_has_baselines);
}

TEST(BenchmarkCampaign, PaperPresetMatchesManuscriptProtocol) {
    const hbrick::ReachabilityBenchmarkConfig config =
        hbrick::benchmarkCampaignConfigFromPreset("paper");
    EXPECT_EQ(config.query_count, 10000U);
    EXPECT_EQ(config.warmup_queries, 1000U);
    EXPECT_EQ(config.correctness_check_count, 10000U);
    EXPECT_EQ(config.max_memory_bytes, 64ULL << 30);
    EXPECT_FALSE(config.closure_enable_projected_speedup_early_stop);
    EXPECT_EQ(config.brick_tile_size.width, 16U);
    EXPECT_EQ(config.hbrick_max_depth, hbrick::kHBrickFullDepth);
    EXPECT_TRUE(config.use_balanced_workload);
    EXPECT_TRUE(config.require_balanced_workload);
    EXPECT_EQ(config.max_preprocess_seconds, 3600U);
    EXPECT_TRUE(config.isolate_methods);
}

TEST(BenchmarkCampaign, ConfigIdIsStable) {
    hbrick::ReachabilityBenchmarkConfig config =
        hbrick::benchmarkCampaignConfigFromPreset("smoke");
    const std::string first = hbrick::benchmarkCampaignConfigId(config);
    const std::string second = hbrick::benchmarkCampaignConfigId(config);
    EXPECT_EQ(first, second);
    EXPECT_FALSE(first.empty());
}

TEST(BenchmarkCampaign, ConfigIdEncodesMemoryCap) {
    hbrick::ReachabilityBenchmarkConfig config =
        hbrick::benchmarkCampaignConfigFromPreset("brick");
    EXPECT_NE(
        hbrick::benchmarkCampaignConfigId(config).find("mem4096m"),
        std::string::npos
    );

    config.max_memory_bytes = 8ULL << 30;
    EXPECT_NE(
        hbrick::benchmarkCampaignConfigId(config).find("mem8192m"),
        std::string::npos
    );
}

TEST(BenchmarkCampaign, BrickTileConfigIdUsesFlatSuffix) {
    const hbrick::ReachabilityBenchmarkConfig base =
        hbrick::benchmarkCampaignConfigFromPreset("brick");
    std::string error;
    const std::vector<hbrick::ReachabilityBenchmarkConfig> tiles =
        hbrick::expandBenchmarkCampaignConfigSweep(base, "brick-tile", error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(tiles.size(), 5U);
    for (const hbrick::ReachabilityBenchmarkConfig& variant : tiles) {
        const std::string id = hbrick::benchmarkCampaignConfigId(variant);
        EXPECT_NE(id.find("_flat"), std::string::npos) << id;
        EXPECT_EQ(id.find("_hg"), std::string::npos) << id;
        EXPECT_EQ(id.find("_dfull"), std::string::npos) << id;
    }

    error.clear();
    const std::vector<hbrick::ReachabilityBenchmarkConfig> hbrick =
        hbrick::expandBenchmarkCampaignConfigSweep(base, "hbrick-group", error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(hbrick.size(), 12U);
    EXPECT_EQ(hbrick.front().brick_tile_size.width, 64U);
    EXPECT_EQ(hbrick.back().brick_tile_size.width, 16U);
    const std::string hbrick_id = hbrick::benchmarkCampaignConfigId(hbrick.front());
    EXPECT_NE(hbrick_id.find("_hg"), std::string::npos) << hbrick_id;
    EXPECT_EQ(hbrick_id.find("_flat"), std::string::npos) << hbrick_id;
    EXPECT_NE(hbrick_id.find("t64x64"), std::string::npos) << hbrick_id;

    error.clear();
    const std::vector<hbrick::ReachabilityBenchmarkConfig> variant_bg =
        hbrick::expandBenchmarkCampaignConfigSweep(base, "hbrick-variant-bg", error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(variant_bg.size(), 28U);
    EXPECT_EQ(variant_bg.front().brick_tile_size.width, 128U);
    EXPECT_EQ(variant_bg.back().brick_tile_size.width, 2U);
    EXPECT_EQ(variant_bg.front().hbrick_group_size.group_w, 2U);
    EXPECT_EQ(variant_bg[3].hbrick_group_size.group_w, 16U);
}

TEST(BenchmarkCampaign, DefaultSweepKeepsExplicitTileAndGroup) {
    hbrick::ReachabilityBenchmarkConfig base =
        hbrick::benchmarkCampaignConfigFromPreset("smoke");
    base.brick_tile_size = hbrick::TileSize{128U, 128U};
    base.hbrick_group_size = hbrick::GroupSize{8U, 8U};
    base.hbrick_max_depth = hbrick::kHBrickFullDepth;
    base.max_memory_bytes = 8ULL << 30;
    std::string error;
    const std::vector<hbrick::ReachabilityBenchmarkConfig> configs =
        hbrick::expandBenchmarkCampaignConfigSweep(base, "default", error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(configs.size(), 1U);
    EXPECT_EQ(
        hbrick::benchmarkCampaignConfigId(configs.front()),
        "t128x128_k0_mem8192m_hg8x8_dfull"
    );
}

TEST(BenchmarkCampaign, AppendMigratesSchemaV3ResultsHeader) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hbrick_campaign_schema_v3.csv";
    std::filesystem::remove(path);

    std::string v3_header = hbrick::benchmarkCampaignResultsCsvHeaderLine();
    const auto column = v3_header.find("measured_retained_index_bytes");
    ASSERT_NE(column, std::string::npos);
    v3_header.replace(column, std::string("measured_retained_index_bytes").size(), "aux_index_bytes");

    {
        std::ofstream output(path);
        output << v3_header;
        output << "3,old_campaign,ts,map,gen,CsrBfs,Completed,,1,0,1,2,3,0,0,0,10,20,0\n";
    }

    hbrick::BenchmarkCampaignResultRow row{};
    row.campaign_id = "schema_v4";
    row.metrics.method = hbrick::ReachabilityBaselineId::CsrBfs;
    row.metrics.status = hbrick::BaselineStatus::Completed;
    row.metrics.measured_retained_index_bytes = 99U;
    const std::vector<hbrick::BenchmarkCampaignResultRow> rows{row};

    std::string error;
    ASSERT_TRUE(hbrick::appendBenchmarkCampaignResultsCsv(path, rows, error)) << error;

    const std::string text = readFile(path);
    EXPECT_NE(text.find("measured_retained_index_bytes"), std::string::npos);
    EXPECT_EQ(text.find("aux_index_bytes"), std::string::npos);
    EXPECT_EQ(text.find("\n3,"), std::string::npos);
    EXPECT_NE(text.find("\n4,old_campaign,"), std::string::npos);
    EXPECT_NE(text.find("\n4,schema_v4,"), std::string::npos);
    EXPECT_EQ(text.front(), 'c');

    std::filesystem::remove(path);
}

TEST(BenchmarkCampaign, AppendRejectsUnknownResultsHeader) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hbrick_campaign_schema_bad.csv";
    std::filesystem::remove(path);
    {
        std::ofstream output(path);
        output << "not_a_campaign_header\n";
    }

    hbrick::BenchmarkCampaignResultRow row{};
    row.campaign_id = "bad";
    const std::vector<hbrick::BenchmarkCampaignResultRow> rows{row};

    std::string error;
    EXPECT_FALSE(hbrick::appendBenchmarkCampaignResultsCsv(path, rows, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(readFile(path).find("not_a_campaign_header"), 0U);

    std::filesystem::remove(path);
}

TEST(BenchmarkCampaign, EnsureAcceptsMissingResultsFile) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hbrick_campaign_schema_missing.csv";
    std::filesystem::remove(path);
    std::string error;
    EXPECT_TRUE(hbrick::ensureBenchmarkCampaignResultsCsvSchema(path, error));
    EXPECT_TRUE(error.empty());
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(BenchmarkCampaign, ResultsCsvHeaderIsGolden) {
    const char* header = hbrick::benchmarkCampaignResultsCsvHeaderLine();
    EXPECT_NE(std::strstr(header, "query_min_ns"), nullptr);
    EXPECT_NE(std::strstr(header, "query_max_ns"), nullptr);
    EXPECT_NE(std::strstr(header, "correctness_failed"), nullptr);
    EXPECT_NE(std::strstr(header, "map_class"), nullptr);
    EXPECT_NE(std::strstr(header, "kleene_rounds_scheduled"), nullptr);
    EXPECT_NE(std::strstr(header, "timer_source"), nullptr);
    EXPECT_NE(std::strstr(header, "measured_retained_index_bytes"), nullptr);
    EXPECT_EQ(std::strstr(header, "aux_index_bytes"), nullptr);
    EXPECT_NE(std::strstr(header, "scratch_bytes"), nullptr);
    EXPECT_NE(std::strstr(header, "hbrick_fallback_rate"), nullptr);
    EXPECT_NE(std::strstr(header, "hbrick_level_stats"), nullptr);
    EXPECT_NE(std::strstr(header, "proc_rss_delta_bytes"), nullptr);
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
    EXPECT_EQ(hbrick::benchmarkCampaignMapClass("open_grid", stats), "open_grid");
}

TEST(BenchmarkCampaign, KleeneOraclePresetIncludesClosureOracles) {
    const hbrick::ReachabilityBenchmarkConfig config =
        hbrick::benchmarkCampaignConfigFromPreset("kleene-oracle");
    ASSERT_EQ(config.methods.size(), 4U);
    EXPECT_EQ(config.methods[0], hbrick::ReachabilityBaselineId::FullClosure);
    EXPECT_EQ(config.methods[1], hbrick::ReachabilityBaselineId::FullClosureKleene);
    EXPECT_EQ(config.methods[2], hbrick::ReachabilityBaselineId::SccDagClosure);
    EXPECT_EQ(config.methods[3], hbrick::ReachabilityBaselineId::BrickClosure);
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
