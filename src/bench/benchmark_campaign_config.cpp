#include "hbrick/bench/benchmark_campaign_config.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "hbrick/tile/hbrick_config.hpp"

namespace hbrick {

namespace {

[[nodiscard]] std::string lowerAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))
        );
    }
    return value;
}

[[nodiscard]] bool equalsIgnoreCase(
    const std::string& left,
    const char* right
) noexcept {
    return lowerAscii(left) == right;
}

void appendUniqueMethod(
    const ReachabilityBaselineId method,
    std::vector<ReachabilityBaselineId>& methods
) {
    if (std::find(methods.begin(), methods.end(), method) == methods.end()) {
        methods.push_back(method);
    }
}

void appendUniqueConfig(
    const ReachabilityBenchmarkConfig& config,
    std::vector<ReachabilityBenchmarkConfig>& configs
) {
    const std::string config_id = benchmarkCampaignConfigId(config);
    for (const ReachabilityBenchmarkConfig& existing : configs) {
        if (benchmarkCampaignConfigId(existing) == config_id) {
            return;
        }
    }
    configs.push_back(config);
}

void expandBrickTileSweep(
    const ReachabilityBenchmarkConfig& base,
    std::vector<ReachabilityBenchmarkConfig>& configs
) {
    static constexpr TileSize kTiles[] = {
        TileSize{4U, 4U},
        TileSize{8U, 8U},
    };
    for (const TileSize tile : kTiles) {
        ReachabilityBenchmarkConfig variant = base;
        variant.brick_tile_size = tile;
        appendUniqueConfig(variant, configs);
    }
}

void expandBrickKleeneSweep(
    const ReachabilityBenchmarkConfig& base,
    std::vector<ReachabilityBenchmarkConfig>& configs
) {
    for (const bool parallel : {false, true}) {
        ReachabilityBenchmarkConfig variant = base;
        variant.kleene_use_parallel = parallel;
        variant.kleene_num_threads = 0U;
        appendUniqueConfig(variant, configs);
    }
}

void expandBrickCombinedSweep(
    const ReachabilityBenchmarkConfig& base,
    std::vector<ReachabilityBenchmarkConfig>& configs
) {
    static constexpr TileSize kTiles[] = {
        TileSize{4U, 4U},
        TileSize{8U, 8U},
    };
    for (const TileSize tile : kTiles) {
        for (const bool parallel : {false, true}) {
            ReachabilityBenchmarkConfig variant = base;
            variant.brick_tile_size = tile;
            variant.kleene_use_parallel = parallel;
            variant.kleene_num_threads = 0U;
            appendUniqueConfig(variant, configs);
        }
    }
}

void expandHbrickSweep(
    const ReachabilityBenchmarkConfig& base,
    std::vector<ReachabilityBenchmarkConfig>& configs
) {
    static constexpr TileSize kTiles[] = {
        TileSize{4U, 4U},
        TileSize{8U, 8U},
    };
    static constexpr uint32_t kDepths[] = {
        2U,
        kHBrickFullDepth,
    };
    for (const TileSize tile : kTiles) {
        for (const uint32_t depth : kDepths) {
            ReachabilityBenchmarkConfig variant = base;
            variant.brick_tile_size = tile;
            variant.hbrick_group_size = GroupSize{2U, 2U};
            variant.hbrick_max_depth = depth;
            appendUniqueConfig(variant, configs);
        }
    }
}

}  // namespace

std::string benchmarkCampaignConfigId(
    const ReachabilityBenchmarkConfig& config
) noexcept {
    std::ostringstream stream;
    stream << "t" << config.brick_tile_size.width << 'x' << config.brick_tile_size.height
           << "_k" << (config.kleene_use_parallel ? 1U : 0U);
    if (config.kleene_use_parallel) {
        stream << "n" << config.kleene_num_threads;
    }
    stream << "_mem" << (config.max_memory_bytes >> 20U) << 'm'
           << "_hg" << config.hbrick_group_size.group_w << 'x'
           << config.hbrick_group_size.group_h;
    if (config.hbrick_max_depth == kHBrickFullDepth) {
        stream << "_dfull";
    } else {
        stream << "_d" << config.hbrick_max_depth;
    }
    if (config.closure_enable_projected_speedup_early_stop) {
        stream << "_ces1";
    }
    return stream.str();
}

BenchmarkCampaignRunParameters benchmarkCampaignRunParametersFromConfig(
    const ReachabilityBenchmarkConfig& config
) noexcept {
    BenchmarkCampaignRunParameters parameters{};
    parameters.config_id = benchmarkCampaignConfigId(config);
    parameters.brick_tile_width = config.brick_tile_size.width;
    parameters.brick_tile_height = config.brick_tile_size.height;
    parameters.max_memory_bytes = config.max_memory_bytes;
    parameters.kleene_use_parallel = config.kleene_use_parallel;
    parameters.kleene_num_threads = config.kleene_num_threads;
    parameters.hbrick_group_width = config.hbrick_group_size.group_w;
    parameters.hbrick_group_height = config.hbrick_group_size.group_h;
    parameters.hbrick_max_depth = config.hbrick_max_depth;
    parameters.closure_early_stop = config.closure_enable_projected_speedup_early_stop;
    return parameters;
}

bool parseBenchmarkCampaignConfigSweepNames(
    const std::string& csv,
    std::vector<std::string>& sweep_names,
    std::string& error_message
) {
    sweep_names.clear();
    if (csv.empty()) {
        sweep_names.push_back("default");
        return true;
    }

    std::istringstream stream(csv);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) {
            sweep_names.push_back(lowerAscii(token));
        }
    }
    if (sweep_names.empty()) {
        error_message = "No config sweep names provided";
        return false;
    }
    return true;
}

std::vector<ReachabilityBenchmarkConfig> expandBenchmarkCampaignConfigSweep(
    const ReachabilityBenchmarkConfig& base,
    const std::string& sweep_name,
    std::string& error_message
) {
    const std::string normalized = lowerAscii(sweep_name);
    std::vector<ReachabilityBenchmarkConfig> configs;

    if (normalized.empty() || normalized == "default") {
        configs.push_back(base);
        return configs;
    }
    if (normalized == "brick-tile") {
        expandBrickTileSweep(base, configs);
        return configs;
    }
    if (normalized == "brick-kleene") {
        expandBrickKleeneSweep(base, configs);
        return configs;
    }
    if (normalized == "brick") {
        expandBrickCombinedSweep(base, configs);
        return configs;
    }
    if (normalized == "hbrick") {
        expandHbrickSweep(base, configs);
        return configs;
    }
    if (normalized == "all") {
        expandBrickCombinedSweep(base, configs);
        expandHbrickSweep(base, configs);
        return configs;
    }

    error_message = "Unknown config sweep: " + sweep_name;
    return {};
}

std::vector<ReachabilityBenchmarkConfig> expandBenchmarkCampaignConfigSweeps(
    const ReachabilityBenchmarkConfig& base,
    const std::span<const std::string> sweep_names,
    std::string& error_message
) {
    std::vector<ReachabilityBenchmarkConfig> configs;
    for (const std::string& sweep_name : sweep_names) {
        const std::vector<ReachabilityBenchmarkConfig> expanded =
            expandBenchmarkCampaignConfigSweep(base, sweep_name, error_message);
        if (!error_message.empty()) {
            return {};
        }
        for (const ReachabilityBenchmarkConfig& config : expanded) {
            appendUniqueConfig(config, configs);
        }
    }
    if (configs.empty()) {
        configs.push_back(base);
    }
    return configs;
}

bool reachabilityBaselineFromName(
    const std::string& name,
    ReachabilityBaselineId& method
) noexcept {
    const std::string normalized = lowerAscii(name);
    if (normalized == "csrbfs" || normalized == "bfs") {
        method = ReachabilityBaselineId::CsrBfs;
        return true;
    }
    if (normalized == "csrdfs" || normalized == "dfs") {
        method = ReachabilityBaselineId::CsrDfs;
        return true;
    }
    if (normalized == "sccdagsearch") {
        method = ReachabilityBaselineId::SccDagSearch;
        return true;
    }
    if (normalized == "sccdagclosure") {
        method = ReachabilityBaselineId::SccDagClosure;
        return true;
    }
    if (normalized == "fullclosure" || normalized == "warshall") {
        method = ReachabilityBaselineId::FullClosure;
        return true;
    }
    if (normalized == "twohop") {
        method = ReachabilityBaselineId::TwoHop;
        return true;
    }
    if (normalized == "grail") {
        method = ReachabilityBaselineId::Grail;
        return true;
    }
    if (normalized == "bricksearch") {
        method = ReachabilityBaselineId::BrickSearch;
        return true;
    }
    if (normalized == "brickclosure") {
        method = ReachabilityBaselineId::BrickClosure;
        return true;
    }
    if (normalized == "hbrick") {
        method = ReachabilityBaselineId::HBrick;
        return true;
    }
    return false;
}

bool parseReachabilityBaselineNames(
    const std::span<const std::string> names,
    std::vector<ReachabilityBaselineId>& methods,
    std::string& error_message
) {
    methods.clear();
    for (const std::string& name : names) {
        ReachabilityBaselineId method = ReachabilityBaselineId::CsrBfs;
        if (!reachabilityBaselineFromName(name, method)) {
            error_message = "Unknown baseline method: " + name;
            return false;
        }
        appendUniqueMethod(method, methods);
    }
    return !methods.empty();
}

bool parseReachabilityBaselineCsv(
    const std::string& csv,
    std::vector<ReachabilityBaselineId>& methods,
    std::string& error_message
) {
    std::vector<std::string> names;
    std::istringstream stream(csv);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) {
            names.push_back(token);
        }
    }
    return parseReachabilityBaselineNames(names, methods, error_message);
}

ReachabilityBenchmarkConfig benchmarkCampaignConfigFromPreset(
    const char* preset
) noexcept {
    ReachabilityBenchmarkConfig config{};
    config.query_count = 256U;
    config.warmup_queries = 16U;
    config.correctness_check_count = 32U;
    config.pair_seed = 0xCA0151ULL;
    config.max_memory_bytes = 512ULL << 20;

    if (preset == nullptr || equalsIgnoreCase(preset, "all")) {
        config.methods = {
            ReachabilityBaselineId::CsrBfs,
            ReachabilityBaselineId::CsrDfs,
            ReachabilityBaselineId::SccDagSearch,
            ReachabilityBaselineId::SccDagClosure,
            ReachabilityBaselineId::TwoHop,
            ReachabilityBaselineId::Grail,
            ReachabilityBaselineId::BrickSearch,
            ReachabilityBaselineId::BrickClosure,
            ReachabilityBaselineId::FullClosure,
            ReachabilityBaselineId::HBrick,
        };
        return config;
    }
    if (equalsIgnoreCase(preset, "smoke")) {
        config.query_count = 64U;
        config.warmup_queries = 8U;
        config.correctness_check_count = 16U;
        config.methods = {
            ReachabilityBaselineId::CsrBfs,
            ReachabilityBaselineId::BrickSearch,
            ReachabilityBaselineId::BrickClosure,
            ReachabilityBaselineId::HBrick,
        };
        return config;
    }
    if (equalsIgnoreCase(preset, "csr")) {
        config.methods = {
            ReachabilityBaselineId::CsrBfs,
            ReachabilityBaselineId::CsrDfs,
            ReachabilityBaselineId::SccDagSearch,
            ReachabilityBaselineId::SccDagClosure,
            ReachabilityBaselineId::TwoHop,
            ReachabilityBaselineId::Grail,
            ReachabilityBaselineId::FullClosure,
        };
        return config;
    }
    if (equalsIgnoreCase(preset, "brick")) {
        config.methods = {
            ReachabilityBaselineId::BrickSearch,
            ReachabilityBaselineId::BrickClosure,
            ReachabilityBaselineId::HBrick,
        };
        return config;
    }
    if (equalsIgnoreCase(preset, "kleene-oracle")) {
        config.query_count = 64U;
        config.warmup_queries = 8U;
        config.correctness_check_count = 32U;
        config.methods = {
            ReachabilityBaselineId::FullClosure,
            ReachabilityBaselineId::SccDagClosure,
            ReachabilityBaselineId::BrickClosure,
        };
        return config;
    }

    config.methods = {
        ReachabilityBaselineId::CsrBfs,
        ReachabilityBaselineId::BrickSearch,
        ReachabilityBaselineId::BrickClosure,
        ReachabilityBaselineId::HBrick,
    };
    return config;
}

}  // namespace hbrick