/**
 * @file movingai_map_catalog.hpp
 * @ingroup hbrick_test_support
 * @brief Discovers extracted MovingAI benchmark maps for parametrized tests.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/io/movingai_loader.hpp"

namespace hbrick::test_support {

/** @brief One map under @c extracted/<set>/maps/. @ingroup hbrick_test_support */
struct MovingAiMapEntry {
    std::string set_name;
    std::string map_name;

    [[nodiscard]] std::string label() const {
        return set_name + '/' + map_name;
    }
};

/**
 * @brief Lists every @c .map file under @p extracted_root/<set>/maps/, sorted by set then name.
 * @ingroup hbrick_test_support
 *
 * Returns an empty vector when @p extracted_root is missing.
 */
[[nodiscard]] std::vector<MovingAiMapEntry> discoverMovingAiMaps(
    const std::filesystem::path& extracted_root
);

/** @brief Passability policy appropriate for a benchmark set slug. @ingroup hbrick_test_support */
[[nodiscard]] MovingAiPassabilityPolicy passabilityPolicyForMovingAiSet(
    const std::string& set_name
);

/** @brief Deterministic orientation params derived from @p set_name and @p map_name. @ingroup hbrick_test_support */
[[nodiscard]] RandomAsymmetricParams randomParamsForMovingAiMap(
    const std::string& set_name,
    const std::string& map_name
);

/**
 * @brief Catalog built once from @c HBRICK_SOURCE_DIR/datasets/movingai/extracted.
 * @ingroup hbrick_test_support
 */
[[nodiscard]] const std::vector<MovingAiMapEntry>& movingAiMapCatalog();

/**
 * @brief Maps used by @c test_movingai_reachability.
 * @ingroup hbrick_test_support
 *
 * When @c HBRICK_FULL_MOVINGAI_TESTS is defined at compile time, returns every
 * discovered map. Otherwise returns one smallest map per benchmark set so the
 * default CTest run stays fast.
 */
[[nodiscard]] const std::vector<MovingAiMapEntry>& movingAiReachabilityTestCatalog();

}  // namespace hbrick::test_support
