#include "hbrick/bench/benchmark_campaign_config.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

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

}  // namespace

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

    config.methods = {
        ReachabilityBaselineId::CsrBfs,
        ReachabilityBaselineId::BrickSearch,
        ReachabilityBaselineId::BrickClosure,
        ReachabilityBaselineId::HBrick,
    };
    return config;
}

}  // namespace hbrick
