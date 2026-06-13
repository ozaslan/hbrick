/**
 * @file recipe_graph.hpp
 * @ingroup hbrick_test_support
 * @brief Build directed CSR graphs from orientation recipes and dataset maps.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "hbrick/graph/csr_graph.hpp"
#include "recipe.hpp"

namespace hbrick::test_support {

/**
 * @brief Converts a saved orientation recipe into @ref hbrick::RandomAsymmetricParams.
 * @ingroup hbrick_test_support
 */
[[nodiscard]] hbrick::RandomAsymmetricParams paramsFromRecipe(const hbrick::tools::Recipe& recipe);

/**
 * @brief Locates `<datasets_root>/<set>/maps/<map>` and builds a directed CSR graph.
 * @ingroup hbrick_test_support
 *
 * @param recipe Recipe naming the dataset set, map file, policy, mode, and seed.
 * @param datasets_root Extracted MovingAI root (contains set subdirectories).
 * @return CSR graph on success, or @c std::nullopt when the map cannot be loaded.
 */
[[nodiscard]] std::optional<hbrick::CsrGraph> buildGraphFromRecipe(
    const hbrick::tools::Recipe& recipe,
    const std::filesystem::path& datasets_root
);

/**
 * @brief Resolves a map path from a recipe relative to @p datasets_root.
 * @ingroup hbrick_test_support
 */
[[nodiscard]] std::filesystem::path mapPathForRecipe(
    const hbrick::tools::Recipe& recipe,
    const std::filesystem::path& datasets_root
);

}  // namespace hbrick::test_support
