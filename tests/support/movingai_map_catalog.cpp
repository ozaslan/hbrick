#include "movingai_map_catalog.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <optional>
#include <unordered_map>

#include "test_limits.hpp"

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

[[nodiscard]] std::optional<uint64_t> readMovingAiMapVertexCount(
    const std::filesystem::path& map_path
) {
    std::ifstream input(map_path);
    if (!input) {
        return std::nullopt;
    }

    uint32_t width = 0U;
    uint32_t height = 0U;
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("width ", 0U) == 0U) {
            width = static_cast<uint32_t>(std::stoul(line.substr(6U)));
        } else if (line.rfind("height ", 0U) == 0U) {
            height = static_cast<uint32_t>(std::stoul(line.substr(7U)));
        } else if (line == "map") {
            break;
        }
    }

    if (width == 0U || height == 0U) {
        return std::nullopt;
    }

    return static_cast<uint64_t>(width) * height;
}

[[nodiscard]] std::vector<MovingAiMapEntry> selectOneSmallestMapPerSet(
    const std::vector<MovingAiMapEntry>& catalog,
    const std::filesystem::path& extracted_root
) {
    struct SetBest {
        MovingAiMapEntry entry;
        uint64_t vertex_count = std::numeric_limits<uint64_t>::max();
    };

    std::unordered_map<std::string, SetBest> best_by_set;
    best_by_set.reserve(catalog.size());

    for (const MovingAiMapEntry& entry : catalog) {
        const std::filesystem::path map_path =
            extracted_root / entry.set_name / "maps" / entry.map_name;
        const std::optional<uint64_t> vertex_count = readMovingAiMapVertexCount(map_path);
        if (!vertex_count.has_value()) {
            continue;
        }

        SetBest& best = best_by_set[entry.set_name];
        if (*vertex_count < best.vertex_count) {
            best.entry = entry;
            best.vertex_count = *vertex_count;
        }
    }

    std::vector<MovingAiMapEntry> smoke_catalog;
    smoke_catalog.reserve(best_by_set.size());
    for (auto& [set_name, best] : best_by_set) {
        (void)set_name;
        if (best.vertex_count > kMovingAiSmokeMapVertexLimit) {
            continue;
        }
        smoke_catalog.push_back(best.entry);
    }

    std::sort(
        smoke_catalog.begin(),
        smoke_catalog.end(),
        [](const MovingAiMapEntry& left, const MovingAiMapEntry& right) {
            if (left.set_name != right.set_name) {
                return left.set_name < right.set_name;
            }
            return left.map_name < right.map_name;
        }
    );

    return smoke_catalog;
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

const std::vector<MovingAiMapEntry>& movingAiReachabilityTestCatalog() {
    static const std::vector<MovingAiMapEntry> catalog = [] {
#ifdef HBRICK_SOURCE_DIR
        const std::filesystem::path extracted_root =
            std::filesystem::path(HBRICK_SOURCE_DIR) / "datasets" / "movingai" / "extracted";
        const std::vector<MovingAiMapEntry> full_catalog = discoverMovingAiMaps(extracted_root);
#ifdef HBRICK_FULL_MOVINGAI_TESTS
        return full_catalog;
#else
        return selectOneSmallestMapPerSet(full_catalog, extracted_root);
#endif
#else
        return std::vector<MovingAiMapEntry>{};
#endif
    }();
    return catalog;
}

}  // namespace hbrick::test_support
