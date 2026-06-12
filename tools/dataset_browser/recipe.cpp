#include "recipe.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace hbrick::tools {

namespace {

const char* policyName(const MovingAiPassabilityPolicy policy) {
    switch (policy) {
        case MovingAiPassabilityPolicy::GroundOnly: return "ground_only";
        case MovingAiPassabilityPolicy::GroundAndSwamp: return "ground_and_swamp";
        case MovingAiPassabilityPolicy::AllTraversable: return "all_traversable";
        case MovingAiPassabilityPolicy::AnyTerrainLetter: return "any_terrain_letter";
    }
    return "ground_only";
}

bool policyFromName(const std::string& name, MovingAiPassabilityPolicy& out) {
    if (name == "ground_only") { out = MovingAiPassabilityPolicy::GroundOnly; return true; }
    if (name == "ground_and_swamp") { out = MovingAiPassabilityPolicy::GroundAndSwamp; return true; }
    if (name == "all_traversable") { out = MovingAiPassabilityPolicy::AllTraversable; return true; }
    if (name == "any_terrain_letter") { out = MovingAiPassabilityPolicy::AnyTerrainLetter; return true; }
    return false;
}

const char* modeName(const GridEdgeConversionMode mode) {
    switch (mode) {
        case GridEdgeConversionMode::RandomAsymmetric: return "random_asymmetric";
        case GridEdgeConversionMode::BidirectionalAll: return "bidirectional_all";
        case GridEdgeConversionMode::AcyclicEastSouth: return "acyclic_east_south";
        case GridEdgeConversionMode::GradientFlow: return "gradient_flow";
    }
    return "random_asymmetric";
}

bool modeFromName(const std::string& name, GridEdgeConversionMode& out) {
    if (name == "random_asymmetric") { out = GridEdgeConversionMode::RandomAsymmetric; return true; }
    if (name == "bidirectional_all") { out = GridEdgeConversionMode::BidirectionalAll; return true; }
    if (name == "acyclic_east_south") { out = GridEdgeConversionMode::AcyclicEastSouth; return true; }
    if (name == "gradient_flow") { out = GridEdgeConversionMode::GradientFlow; return true; }
    return false;
}

/**
 * @brief Finds `"key":` in flat JSON and returns the raw value token.
 *
 * The format is fully controlled by @ref saveRecipe (flat object, no nesting,
 * no escapes inside our string values), so a targeted scan is sufficient and
 * avoids a JSON library dependency.
 */
bool rawValue(const std::string& text, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = text.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    ++pos;
    while (pos < text.size()
           && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
        ++pos;
    }
    if (pos >= text.size()) {
        return false;
    }

    if (text[pos] == '"') {
        const std::size_t end = text.find('"', pos + 1);
        if (end == std::string::npos) {
            return false;
        }
        out = text.substr(pos + 1, end - pos - 1);
        return true;
    }

    std::size_t end = pos;
    while (end < text.size() && text[end] != ',' && text[end] != '}'
           && std::isspace(static_cast<unsigned char>(text[end])) == 0) {
        ++end;
    }
    out = text.substr(pos, end - pos);
    return !out.empty();
}

bool stringValue(const std::string& text, const std::string& key, std::string& out) {
    return rawValue(text, key, out);
}

bool floatValue(const std::string& text, const std::string& key, float& out) {
    std::string raw;
    if (!rawValue(text, key, raw)) {
        return false;
    }
    try {
        out = std::stof(raw);
    } catch (...) {
        return false;
    }
    return true;
}

bool seedValue(const std::string& text, const std::string& key, uint64_t& out) {
    std::string raw;
    if (!rawValue(text, key, raw)) {
        return false;
    }
    try {
        out = std::stoull(raw, nullptr, 0);
    } catch (...) {
        return false;
    }
    return true;
}

std::string sanitizeForFilename(const std::string& name) {
    std::string result = name;
    for (char& c : result) {
        const bool keep = (std::isalnum(static_cast<unsigned char>(c)) != 0)
            || c == '-' || c == '_' || c == '.';
        if (!keep) {
            c = '_';
        }
    }
    return result;
}

}  // namespace

std::filesystem::path saveRecipe(
    const std::filesystem::path& directory,
    const Recipe& recipe
) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return {};
    }

    char seed_hex[32];
    std::snprintf(
        seed_hex, sizeof(seed_hex), "0x%016llx",
        static_cast<unsigned long long>(recipe.seed)
    );

    std::ostringstream body;
    body << "{\n"
         << "  \"schema\": \"hbrick_orientation_recipe_v1\",\n"
         << "  \"set\": \"" << recipe.set_name << "\",\n"
         << "  \"map\": \"" << recipe.map_name << "\",\n"
         << "  \"policy\": \"" << policyName(recipe.policy) << "\",\n"
         << "  \"mode\": \"" << modeName(recipe.mode) << "\",\n"
         << "  \"seed\": \"" << seed_hex << "\",\n"
         << "  \"p_one_way\": " << recipe.p_one_way << ",\n"
         << "  \"p_bidirectional\": " << recipe.p_bidirectional << ",\n"
         << "  \"gradient_angle_degrees\": " << recipe.gradient_angle_degrees << ",\n"
         << "  \"p_against_gradient\": " << recipe.p_against_gradient << "\n"
         << "}\n";

    // FNV-1a over the body: different parameters always get different file
    // names, while re-saving the identical recipe overwrites itself.
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char c : body.str()) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001B3ULL;
    }
    char hash_hex[16];
    std::snprintf(
        hash_hex, sizeof(hash_hex), "%08x",
        static_cast<unsigned int>(hash & 0xFFFFFFFFULL)
    );

    const std::string filename = sanitizeForFilename(recipe.set_name) + "__"
        + sanitizeForFilename(recipe.map_name) + "__" + modeName(recipe.mode)
        + "__" + (seed_hex + 2) + "__" + hash_hex + ".json";
    const std::filesystem::path path = directory / filename;

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return {};
    }
    out << body.str();
    return out ? path : std::filesystem::path{};
}

std::optional<Recipe> loadRecipe(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    Recipe recipe;
    std::string policy_name;
    std::string mode_name;
    const bool ok = stringValue(text, "set", recipe.set_name)
        && stringValue(text, "map", recipe.map_name)
        && stringValue(text, "policy", policy_name)
        && policyFromName(policy_name, recipe.policy)
        && stringValue(text, "mode", mode_name)
        && modeFromName(mode_name, recipe.mode)
        && seedValue(text, "seed", recipe.seed);
    if (!ok) {
        return std::nullopt;
    }

    // Probability fields default when absent so older recipes keep loading.
    floatValue(text, "p_one_way", recipe.p_one_way);
    floatValue(text, "p_bidirectional", recipe.p_bidirectional);
    floatValue(text, "gradient_angle_degrees", recipe.gradient_angle_degrees);
    floatValue(text, "p_against_gradient", recipe.p_against_gradient);
    return recipe;
}

std::vector<std::filesystem::path> listRecipes(
    const std::filesystem::path& directory
) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    std::filesystem::directory_iterator it(directory, ec);
    if (ec) {
        return files;
    }
    for (const std::filesystem::directory_entry& entry : it) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace hbrick::tools
