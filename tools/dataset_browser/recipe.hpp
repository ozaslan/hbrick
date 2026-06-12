/**
 * @file recipe.hpp
 * @brief Save/load of orientation recipes: the parameters (not the maze) that
 *        regenerate a directed-graph configuration from a dataset map.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/io/movingai_map.hpp"

namespace hbrick::tools {

/** @brief Everything needed to reproduce one directed-graph configuration. */
struct Recipe {
    std::string set_name;
    std::string map_name;
    MovingAiPassabilityPolicy policy = MovingAiPassabilityPolicy::GroundOnly;
    GridEdgeConversionMode mode = GridEdgeConversionMode::RandomAsymmetric;
    uint64_t seed = 1;
    float p_one_way = 0.30F;
    float p_bidirectional = 0.10F;
    float gradient_angle_degrees = 45.0F;
    float p_against_gradient = 0.02F;
};

/**
 * @brief Writes @p recipe as flat JSON under @p directory.
 *
 * The directory is created if missing. The filename derives from the map name
 * plus seed so repeated saves of different seeds do not collide.
 *
 * @return Path of the written file, or empty path on I/O failure.
 */
[[nodiscard]] std::filesystem::path saveRecipe(
    const std::filesystem::path& directory,
    const Recipe& recipe
);

/** @brief Parses a recipe file written by @ref saveRecipe. */
[[nodiscard]] std::optional<Recipe> loadRecipe(const std::filesystem::path& file);

/** @brief Lists `*.json` recipe files in @p directory, sorted by name. */
[[nodiscard]] std::vector<std::filesystem::path> listRecipes(
    const std::filesystem::path& directory
);

}  // namespace hbrick::tools
