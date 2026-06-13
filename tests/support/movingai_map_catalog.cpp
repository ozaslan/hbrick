#include "movingai_map_catalog.hpp"

#include <algorithm>

namespace hbrick::test_support {

namespace {

[[nodiscard]] uint64_t hashCatalogKey(const std::string& set_name, const std::string& map_name) {
    uint64_t hash = 0xA11CE000ULL;
    for (const char character : set_name) {
        hash = hash * 131ULL + static_cast<unsigned char>(character);
    }
    hash = hash * 131ULL + static_cast<unsigned char>('/');
    for (const char character : map_name) {
        hash = hash * 131ULL + static_cast<unsigned char>(character);
    }
    return hash;
}

}  // namespace

std::vector<MovingAiMapEntry> discoverMovingAiMaps(const std::filesystem::path& extracted_root) {
    std::vector<MovingAiMapEntry> entries;
    if (!std::filesystem::is_directory(extracted_root)) {
        return entries;
    }

    for (const std::filesystem::directory_entry& set_entry :
         std::filesystem::directory_iterator(extracted_root)) {
        if (!set_entry.is_directory()) {
            continue;
        }

        const std::filesystem::path maps_dir = set_entry.path() / "maps";
        if (!std::filesystem::is_directory(maps_dir)) {
            continue;
        }

        const std::string set_name = set_entry.path().filename().string();
        for (const std::filesystem::directory_entry& map_entry :
             std::filesystem::directory_iterator(maps_dir)) {
            if (!map_entry.is_regular_file() || map_entry.path().extension() != ".map") {
                continue;
            }

            entries.push_back(MovingAiMapEntry{
                set_name,
                map_entry.path().filename().string(),
            });
        }
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const MovingAiMapEntry& left, const MovingAiMapEntry& right) {
            if (left.set_name != right.set_name) {
                return left.set_name < right.set_name;
            }
            return left.map_name < right.map_name;
        }
    );

    return entries;
}

MovingAiPassabilityPolicy passabilityPolicyForMovingAiSet(const std::string& set_name) {
    if (set_name == "weighted") {
        return MovingAiPassabilityPolicy::AnyTerrainLetter;
    }
    return MovingAiPassabilityPolicy::GroundOnly;
}

RandomAsymmetricParams randomParamsForMovingAiMap(
    const std::string& set_name,
    const std::string& map_name
) {
    return RandomAsymmetricParams{
        hashCatalogKey(set_name, map_name),
        0.12L,
        0.22L,
    };
}

const std::vector<MovingAiMapEntry>& movingAiMapCatalog() {
    static const std::vector<MovingAiMapEntry> catalog = [] {
#ifdef HBRICK_SOURCE_DIR
        return discoverMovingAiMaps(
            std::filesystem::path(HBRICK_SOURCE_DIR) / "datasets" / "movingai" / "extracted"
        );
#else
        return std::vector<MovingAiMapEntry>{};
#endif
    }();
    return catalog;
}

}  // namespace hbrick::test_support
