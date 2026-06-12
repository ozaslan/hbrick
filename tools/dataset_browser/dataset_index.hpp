/**
 * @file dataset_index.hpp
 * @brief Read-only inventory of the extracted MovingAI dataset tree.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hbrick::tools {

/** @brief One .map file on disk. */
struct MapEntry {
    std::string name;
    std::filesystem::path path;
};

/** @brief One benchmark set (dao, street, maze, ...). */
struct SetEntry {
    std::string name;
    std::vector<MapEntry> maps;
};

/** @brief All sets found under the extracted dataset root. */
struct DatasetIndex {
    std::filesystem::path root;
    std::vector<SetEntry> sets;

    [[nodiscard]] std::size_t totalMaps() const noexcept {
        std::size_t count = 0;
        for (const SetEntry& set : sets) {
            count += set.maps.size();
        }
        return count;
    }
};

/**
 * @brief Scans @p root for `<set>/maps/*.map` files, sorted by name.
 *
 * Missing or empty roots yield an index with zero sets; the GUI reports that
 * state instead of failing.
 */
[[nodiscard]] DatasetIndex scanDatasets(const std::filesystem::path& root);

}  // namespace hbrick::tools
