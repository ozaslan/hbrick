#include "recipe_graph.hpp"

#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/io/movingai_loader.hpp"

namespace hbrick::test_support {

RandomAsymmetricParams paramsFromRecipe(const tools::Recipe& recipe) {
    RandomAsymmetricParams params;
    params.seed = recipe.seed;
    params.p_one_way = static_cast<long double>(recipe.p_one_way);
    params.p_bidirectional = static_cast<long double>(recipe.p_bidirectional);
    params.gradient_angle_degrees = static_cast<double>(recipe.gradient_angle_degrees);
    params.p_against_gradient = static_cast<long double>(recipe.p_against_gradient);
    return params;
}

std::filesystem::path mapPathForRecipe(
    const tools::Recipe& recipe,
    const std::filesystem::path& datasets_root
) {
    return datasets_root / recipe.set_name / "maps" / recipe.map_name;
}

std::optional<CsrGraph> buildGraphFromRecipe(
    const tools::Recipe& recipe,
    const std::filesystem::path& datasets_root
) {
    const std::filesystem::path map_path = mapPathForRecipe(recipe, datasets_root);
    const MovingAiLoadResult loaded = loadMovingAiMap(map_path);
    if (!loaded.ok()) {
        return std::nullopt;
    }

    const MazeLayout layout = loaded.map.toMazeLayout(recipe.policy);
    return DirectedGridGraphBuilder::build(
        layout,
        recipe.mode,
        paramsFromRecipe(recipe)
    ).csrGraph();
}

}  // namespace hbrick::test_support
