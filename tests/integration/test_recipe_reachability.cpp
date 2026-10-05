#include <gtest/gtest.h>

#include <filesystem>
#include <limits>
#include <optional>
#include <string>

#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/io/movingai_loader.hpp"
#include "hbrick/io/recipe.hpp"
#include "recipe_graph.hpp"
#include "reachability_oracle.hpp"
#include "test_limits.hpp"

namespace {

[[nodiscard]] std::filesystem::path datasetsRoot() {
    return std::filesystem::path(HBRICK_SOURCE_DIR) / "datasets" / "movingai";
}

[[nodiscard]] std::filesystem::path recipesRoot() {
    return std::filesystem::path(HBRICK_SOURCE_DIR) / "recipes";
}

[[nodiscard]] bool datasetsAvailable() {
    return std::filesystem::is_directory(datasetsRoot());
}

[[nodiscard]] std::filesystem::path committedBostonRecipePath() {
    return recipesRoot()
        / "M01_V06_gf_ang045_back05__street__Boston_0_256.map__gradient_flow__0000000000000001__2b43267c.json";
}

hbrick::tools::Recipe makeSampleRecipe() {
    hbrick::tools::Recipe recipe;
    recipe.set_name = "dao";
    recipe.map_name = "arena2.map";
    recipe.policy = hbrick::MovingAiPassabilityPolicy::GroundOnly;
    recipe.mode = hbrick::GridEdgeConversionMode::RandomAsymmetric;
    recipe.seed = 0xDEADBEEFULL;
    recipe.p_one_way = 0.25F;
    recipe.p_bidirectional = 0.15F;
    recipe.gradient_angle_degrees = 45.0F;
    recipe.p_against_gradient = 0.05F;
    recipe.group = "paper";
    recipe.label = "sample_paper_recipe";
    return recipe;
}

}  // namespace

TEST(RecipePersistence, SaveLoadRoundTripPreservesFields) {
    const std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path() / "hbrick_recipe_test";
    std::filesystem::create_directories(temp_dir);

    const hbrick::tools::Recipe original = makeSampleRecipe();
    const std::filesystem::path written = hbrick::tools::saveRecipe(temp_dir, original);
    ASSERT_FALSE(written.empty());

    const std::optional<hbrick::tools::Recipe> loaded = hbrick::tools::loadRecipe(written);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->set_name, original.set_name);
    EXPECT_EQ(loaded->map_name, original.map_name);
    EXPECT_EQ(loaded->policy, original.policy);
    EXPECT_EQ(loaded->mode, original.mode);
    EXPECT_EQ(loaded->seed, original.seed);
    EXPECT_FLOAT_EQ(loaded->p_one_way, original.p_one_way);
    EXPECT_FLOAT_EQ(loaded->p_bidirectional, original.p_bidirectional);
    EXPECT_FLOAT_EQ(loaded->gradient_angle_degrees, original.gradient_angle_degrees);
    EXPECT_FLOAT_EQ(loaded->p_against_gradient, original.p_against_gradient);
    EXPECT_EQ(loaded->group, original.group);
    EXPECT_EQ(loaded->label, original.label);

    EXPECT_EQ(written.parent_path().filename().string(), original.group);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
}

TEST(RecipeReachabilityOracle, CommittedBostonRecipeBuildsGraphOnAllSlices) {
    if (!datasetsAvailable()) {
        GTEST_SKIP() << "datasets/movingai not present";
    }

    const std::filesystem::path recipe_path = committedBostonRecipePath();
    if (!std::filesystem::exists(recipe_path)) {
        GTEST_SKIP() << "committed recipe fixture missing";
    }

    const std::optional<hbrick::tools::Recipe> recipe = hbrick::tools::loadRecipe(recipe_path);
    ASSERT_TRUE(recipe.has_value());

    const hbrick::MovingAiLoadResult loaded = hbrick::loadMovingAiMap(
        hbrick::test_support::mapPathForRecipe(*recipe, datasetsRoot())
    );
    ASSERT_TRUE(loaded.ok()) << loaded.error;

    const hbrick::MazeLayout layout = loaded.map.toMazeLayout(recipe->policy);
    const std::optional<hbrick::CsrGraph> graph =
        hbrick::test_support::buildGraphFromRecipe(*recipe, datasetsRoot());
    ASSERT_TRUE(graph.has_value()) << recipe->set_name << "/" << recipe->map_name;

    const std::string context = "recipe:" + recipe->map_name + " set=" + recipe->set_name
        + " vertices=" + std::to_string(graph->numVertices());

    hbrick::test_support::expectReachabilityOracleSampledPairs(
        *graph,
        context,
        hbrick::test_support::kIntegrationReachabilitySamplePairCount,
        std::numeric_limits<uint64_t>::max(),
        &layout
    );
}

TEST(RecipeReachabilityOracle, SavedRecipeMatchesDirectBuilderOnArenaMap) {
    if (!datasetsAvailable()) {
        GTEST_SKIP() << "datasets/movingai not present";
    }

    const hbrick::tools::Recipe recipe = makeSampleRecipe();
    const std::optional<hbrick::CsrGraph> from_recipe =
        hbrick::test_support::buildGraphFromRecipe(recipe, datasetsRoot());
    ASSERT_TRUE(from_recipe.has_value());

    const hbrick::MovingAiLoadResult loaded = hbrick::loadMovingAiMap(
        hbrick::test_support::mapPathForRecipe(recipe, datasetsRoot())
    );
    ASSERT_TRUE(loaded.ok()) << loaded.error;

    const hbrick::MazeLayout layout = loaded.map.toMazeLayout(recipe.policy);
    const hbrick::CsrGraph direct = hbrick::DirectedGridGraphBuilder::build(
        layout,
        recipe.mode,
        hbrick::test_support::paramsFromRecipe(recipe)
    ).csrGraph();

    ASSERT_EQ(from_recipe->numVertices(), direct.numVertices());
    ASSERT_EQ(from_recipe->numEdges(), direct.numEdges());

    hbrick::test_support::expectReachabilityOracleSampledPairs(
        direct,
        "dao-arena-recipe vertices=" + std::to_string(direct.numVertices()),
        hbrick::test_support::kIntegrationReachabilitySamplePairCount,
        std::numeric_limits<uint64_t>::max(),
        &layout
    );
}
