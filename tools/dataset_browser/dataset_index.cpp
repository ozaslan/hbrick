#include "dataset_index.hpp"

#include <algorithm>

namespace hbrick::tools {

DatasetIndex scanDatasets(const std::filesystem::path& root) {
    DatasetIndex index;
    index.root = root;

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        return index;
    }

    for (const auto& set_dir : std::filesystem::directory_iterator(root, ec)) {
        if (!set_dir.is_directory()) {
            continue;
        }
        const std::filesystem::path maps_dir = set_dir.path() / "maps";
        if (!std::filesystem::is_directory(maps_dir, ec)) {
            continue;
        }

        SetEntry set;
        set.name = set_dir.path().filename().string();
        for (const auto& file : std::filesystem::directory_iterator(maps_dir, ec)) {
            if (!file.is_regular_file() || file.path().extension() != ".map") {
                continue;
            }
            set.maps.push_back(MapEntry{file.path().filename().string(), file.path()});
        }
        if (set.maps.empty()) {
            continue;
        }

        std::sort(
            set.maps.begin(),
            set.maps.end(),
            [](const MapEntry& a, const MapEntry& b) { return a.name < b.name; }
        );
        index.sets.push_back(std::move(set));
    }

    std::sort(
        index.sets.begin(),
        index.sets.end(),
        [](const SetEntry& a, const SetEntry& b) { return a.name < b.name; }
    );
    return index;
}

}  // namespace hbrick::tools
