#include "hbrick/bit/kleene_squaring_options.hpp"

#include <algorithm>
#include <thread>

namespace hbrick {

namespace {

constexpr uint32_t kMaxKleeneThreads = 64U;

}  // namespace

uint32_t resolveKleeneThreadCount(const KleeneSquaringOptions& options) noexcept {
    if (!options.use_parallel) {
        return 1U;
    }

    uint32_t threads = options.num_threads;
    if (threads == 0U) {
        const unsigned int hardware = std::thread::hardware_concurrency();
        threads = hardware > 0U ? static_cast<uint32_t>(hardware) : 1U;
    }

    return std::max(1U, std::min(threads, kMaxKleeneThreads));
}

ClosureAlgorithm resolveClosureAlgorithm(
    const KleeneSquaringOptions& options,
    const uint32_t num_vertices
) noexcept {
    // 1. Check runtime environment variable override if present
    const char* const env_alg = std::getenv("HBRICK_CLOSURE_ALGORITHM");
    ClosureAlgorithm base_alg = options.algorithm;
    if (env_alg != nullptr && env_alg[0] != '\0') {
        const std::string_view s(env_alg);
        if (s == "gustavson" || s == "legacy" || s == "squaring") {
            return ClosureAlgorithm::GustavsonSquaring;
        } else if (s == "warshall") {
            return ClosureAlgorithm::Warshall;
        } else if (s == "m4rm" || s == "m4rm4" || s == "m4rm_4" || s == "tetra") {
            return ClosureAlgorithm::M4RM4Table;
        } else if (s == "m4rm8" || s == "m4rm_8" || s == "octa") {
            return ClosureAlgorithm::M4RM8Table;
        } else if (s == "hybrid") {
            base_alg = ClosureAlgorithm::Hybrid;
        }
    }

    // 2. If strategy is Hybrid, adaptively select based on matrix dimension
    if (base_alg == ClosureAlgorithm::Hybrid) {
        uint32_t threshold = options.hybrid_warshall_threshold;
        const char* const env_thresh = std::getenv("HBRICK_CLOSURE_THRESHOLD");
        if (env_thresh != nullptr && env_thresh[0] != '\0') {
            const long val = std::strtol(env_thresh, nullptr, 10);
            if (val > 0) {
                threshold = static_cast<uint32_t>(val);
            }
        }

        if (num_vertices <= threshold) {
            return ClosureAlgorithm::Warshall;
        }

        if (options.use_parallel && resolveKleeneThreadCount(options) >= 8U) {
            return ClosureAlgorithm::M4RM8Table;
        }
        return ClosureAlgorithm::M4RM4Table;
    }

    return base_alg;
}

}  // namespace hbrick
