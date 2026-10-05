#include <gtest/gtest.h>

#include <cctype>
#include <filesystem>
#include <limits>
#include <string>

#include "hbrick/baselines/brick_search_baseline.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/io/movingai_loader.hpp"
#include "movingai_map_catalog.hpp"
#include "reachability_oracle.hpp"
#include "test_limits.hpp"

namespace {

[[nodiscard]] std::filesystem::path datasetsRoot() {
    return std::filesystem::path(HBRICK_SOURCE_DIR) / "datasets" / "movingai";
}

void runCatalogMapOracle(const hbrick::test_support::MovingAiMapEntry& entry) {
    const std::filesystem::path map_path =
        datasetsRoot() / entry.set_name / "maps" / entry.map_name;
    ASSERT_TRUE(std::filesystem::exists(map_path)) << map_path.string();

    const hbrick::MovingAiLoadResult loaded = hbrick::loadMovingAiMap(map_path);
    ASSERT_TRUE(loaded.ok()) << entry.label();

    const hbrick::MovingAiPassabilityPolicy policy =
        hbrick::test_support::passabilityPolicyForMovingAiSet(entry.set_name);
    const hbrick::MazeLayout layout = loaded.map.toMazeLayout(policy);
    const hbrick::CsrGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::test_support::randomParamsForMovingAiMap(entry.set_name, entry.map_name)
    ).csrGraph();

    const std::string context = entry.label() + " vertices=" + std::to_string(graph.numVertices());

    hbrick::test_support::expectReachabilityOracleSampledPairs(
        graph,
        context,
        hbrick::test_support::kIntegrationReachabilitySamplePairCount,
        std::numeric_limits<uint64_t>::max(),
        &layout
    );
}

class MovingAiExtractedMapReachabilityTest
    : public ::testing::TestWithParam<hbrick::test_support::MovingAiMapEntry> {};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MovingAiExtractedMapReachabilityTest);

struct MovingAiMapParamName {
    [[nodiscard]] std::string operator()(
        const ::testing::TestParamInfo<hbrick::test_support::MovingAiMapEntry>& info
    ) const {
        std::string name = info.param.set_name + '_' + info.param.map_name;
        for (char& character : name) {
            if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_') {
                character = '_';
            }
        }
        return name;
    }
};

}  // namespace

TEST(MovingAiReachabilityOracle, RequiresExtractedDataset) {
    if (!std::filesystem::is_directory(datasetsRoot())) {
        GTEST_SKIP() << "datasets/movingai not present";
    }

    ASSERT_FALSE(hbrick::test_support::discoverMovingAiMaps(datasetsRoot()).empty())
        << "map root exists but no .map files were discovered";
}

TEST_P(MovingAiExtractedMapReachabilityTest, PassesReachabilityOracleForCatalogMap) {
    if (!std::filesystem::is_directory(datasetsRoot())) {
        GTEST_SKIP() << "datasets/movingai not present";
    }

    runCatalogMapOracle(GetParam());
}

INSTANTIATE_TEST_SUITE_P(
    AllExtractedMaps,
    MovingAiExtractedMapReachabilityTest,
    ::testing::ValuesIn(hbrick::test_support::movingAiReachabilityTestCatalog()),
    MovingAiMapParamName()
);

TEST(MovingAiReachabilityOracle, RealWorldCityStreetShanghaiScale) {
    if (!std::filesystem::is_directory(datasetsRoot())) {
        GTEST_SKIP() << "datasets/movingai not present";
    }

    // Real-world 256x256 city street navigation map (65,536 cells)
    const std::filesystem::path street_path =
        datasetsRoot() / "street" / "maps" / "Shanghai_0_256.map";
    if (!std::filesystem::exists(street_path)) {
        GTEST_SKIP() << "Shanghai_0_256.map not found";
    }

    const hbrick::MovingAiLoadResult loaded = hbrick::loadMovingAiMap(street_path);
    ASSERT_TRUE(loaded.ok());
    const hbrick::MazeLayout layout = loaded.map.toMazeLayout(
        hbrick::MovingAiPassabilityPolicy::GroundOnly
    );
    const hbrick::CsrGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::test_support::randomParamsForMovingAiMap("street", "Shanghai_0_256.map")
    ).csrGraph();

    hbrick::test_support::expectReachabilityOracleSampledPairs(
        graph,
        "street/Shanghai_0_256.map (256x256 city grid)",
        500U,
        std::numeric_limits<uint64_t>::max(),
        &layout
    );
}

TEST(MovingAiReachabilityOracle, HBrickAndBrickSearchOnRealCityStreetShanghai256) {
    if (!std::filesystem::is_directory(datasetsRoot())) {
        GTEST_SKIP() << "datasets/movingai not present";
    }

    const std::filesystem::path street_path =
        datasetsRoot() / "street" / "maps" / "Shanghai_0_256.map";
    if (!std::filesystem::exists(street_path)) {
        GTEST_SKIP() << "Shanghai_0_256.map not found";
    }

    const hbrick::MovingAiLoadResult loaded = hbrick::loadMovingAiMap(street_path);
    ASSERT_TRUE(loaded.ok());
    const hbrick::MazeLayout layout = loaded.map.toMazeLayout(
        hbrick::MovingAiPassabilityPolicy::GroundOnly
    );
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::test_support::randomParamsForMovingAiMap("street", "Shanghai_0_256.map")
    );

    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{16U, 16U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    config.max_depth = 4U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickBaseline hbrick;
    hbrick.preprocess(graph, layout, config);
    ASSERT_EQ(hbrick.status(), hbrick::BaselineStatus::Completed);

    hbrick::BrickSearchBaseline brick_search;
    brick_search.preprocess(
        graph,
        layout,
        hbrick::TileSize{16U, 16U},
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(brick_search.status(), hbrick::BaselineStatus::Completed);

    // Sample 200 distributed pairs across the 256x256 city grid
    hbrick::GraphSearchScratch scratch(graph.numVertices());
    const uint64_t total_pairs = static_cast<uint64_t>(graph.numVertices()) * graph.numVertices();
    const uint64_t step = total_pairs / 200ULL;

    for (uint64_t idx = 0ULL; idx < total_pairs; idx += step) {
        const uint32_t s = static_cast<uint32_t>(idx / graph.numVertices());
        const uint32_t t = static_cast<uint32_t>(idx % graph.numVertices());

        const hbrick::ReachabilityAnswer expected =
            hbrick::Bfs::reachable(graph.csrGraph(), s, t, scratch);
        EXPECT_EQ(hbrick.query(s, t), expected)
            << "HBrick query mismatch on Shanghai (256x256) s=" << s << " t=" << t;
        EXPECT_EQ(brick_search.query(s, t), expected)
            << "BrickSearch query mismatch on Shanghai (256x256) s=" << s << " t=" << t;
    }
}
