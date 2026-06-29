#include <gtest/gtest.h>

#include <cctype>
#include <filesystem>
#include <limits>
#include <string>

#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/io/movingai_loader.hpp"
#include "movingai_map_catalog.hpp"
#include "reachability_oracle.hpp"
#include "test_limits.hpp"

namespace {

[[nodiscard]] std::filesystem::path datasetsRoot() {
    return std::filesystem::path(HBRICK_SOURCE_DIR) / "datasets" / "movingai" / "extracted";
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
        GTEST_SKIP() << "datasets/movingai/extracted not present; "
                        "run datasets/movingai/fetch_movingai.sh --extract-only";
    }

    ASSERT_FALSE(hbrick::test_support::movingAiReachabilityTestCatalog().empty())
        << "extracted tree exists but no .map files were discovered";
}

TEST_P(MovingAiExtractedMapReachabilityTest, PassesReachabilityOracleForCatalogMap) {
    if (!std::filesystem::is_directory(datasetsRoot())) {
        GTEST_SKIP() << "datasets/movingai/extracted not present";
    }

    runCatalogMapOracle(GetParam());
}

INSTANTIATE_TEST_SUITE_P(
    AllExtractedMaps,
    MovingAiExtractedMapReachabilityTest,
    ::testing::ValuesIn(hbrick::test_support::movingAiReachabilityTestCatalog()),
    MovingAiMapParamName()
);
