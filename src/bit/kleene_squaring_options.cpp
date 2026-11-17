#include "hbrick/bit/kleene_squaring_options.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>
#include <string_view>
#include <thread>
#include <vector>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/bit/boolean_closure.hpp"

namespace hbrick {

namespace {

constexpr uint32_t kMaxKleeneThreads = 64U;
constexpr uint32_t kInitialDefaultHybridThreshold = 512U;

std::atomic<uint32_t> g_default_hybrid_threshold{kInitialDefaultHybridThreshold};

[[nodiscard]] BitMatrix buildSyntheticLatticeRelation(
    const uint32_t dimension,
    const uint64_t seed
) {
    BitMatrix relation(dimension, dimension);
    for (uint32_t v = 0U; v < dimension; ++v) {
        relation.set(v, v);
    }
    if (dimension <= 1U) {
        return relation;
    }

    uint32_t side = static_cast<uint32_t>(std::sqrt(static_cast<double>(dimension)));
    while (static_cast<uint64_t>(side) * side < dimension) {
        ++side;
    }

    std::mt19937_64 rng(seed ^ (static_cast<uint64_t>(dimension) * 0x9E3779B97F4A7C15ULL));
    std::uniform_int_distribution<uint32_t> pct(0U, 99U);

    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};

    for (uint32_t u = 0U; u < dimension; ++u) {
        const int ux = static_cast<int>(u % side);
        const int uy = static_cast<int>(u / side);
        for (int dir = 0; dir < 4; ++dir) {
            const int vx = ux + dx[dir];
            const int vy = uy + dy[dir];
            if (vx < 0 || vx >= static_cast<int>(side) || vy < 0 || vy >= static_cast<int>(side)) {
                continue;
            }
            const uint64_t v64 = static_cast<uint64_t>(vy) * side + static_cast<uint64_t>(vx);
            if (v64 >= dimension) {
                continue;
            }
            if (pct(rng) < 85U) {
                relation.set(u, static_cast<uint32_t>(v64));
            }
        }
    }

    return relation;
}

[[nodiscard]] uint64_t medianNanoseconds(std::vector<uint64_t>& samples) noexcept {
    if (samples.empty()) {
        return 1U;
    }
    std::sort(samples.begin(), samples.end());
    const size_t mid = samples.size() / 2U;
    if ((samples.size() & 1U) == 1U) {
        return std::max<uint64_t>(1U, samples[mid]);
    }
    return std::max<uint64_t>(1U, samples[mid - 1U] + (samples[mid] - samples[mid - 1U]) / 2U);
}

[[nodiscard]] uint32_t absDiffU32(const uint32_t a, const uint32_t b) noexcept {
    return a >= b ? (a - b) : (b - a);
}

}  // namespace

uint32_t defaultHybridWarshallThreshold() noexcept {
    return g_default_hybrid_threshold.load(std::memory_order_relaxed);
}

void setDefaultHybridWarshallThreshold(const uint32_t threshold) noexcept {
    g_default_hybrid_threshold.store(std::max(1U, threshold), std::memory_order_relaxed);
}

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

HybridThresholdCalibrationResult calibrateHybridWarshallThreshold(
    const HybridThresholdCalibrationOptions& options
) {
    const uint32_t min_dim = std::max(16U, options.min_dimension);
    const uint32_t max_dim = std::max(min_dim, options.max_dimension);
    const uint32_t step = std::max(8U, options.dimension_step);
    const uint32_t measured_runs = std::max(1U, options.measured_runs);

    KleeneSquaringOptions m4rm_opts = options.base_options;
    const uint32_t m4rm_tables =
        (m4rm_opts.use_parallel && resolveKleeneThreadCount(m4rm_opts) >= 8U) ? 8U : 4U;
    m4rm_opts.algorithm =
        (m4rm_tables == 8U) ? ClosureAlgorithm::M4RM8Table : ClosureAlgorithm::M4RM4Table;

    HybridThresholdCalibrationResult result{};
    result.samples.reserve(static_cast<size_t>((max_dim - min_dim) / step) + 1U);

    BitMatrix work{};
    BitMatrix scratch{};
    std::vector<uint64_t> warshall_times;
    std::vector<uint64_t> m4rm_times;
    warshall_times.reserve(measured_runs);
    m4rm_times.reserve(measured_runs);

    for (uint32_t dim = min_dim; dim <= max_dim;) {
        const BitMatrix base = buildSyntheticLatticeRelation(dim, options.seed);
        const uint32_t squaring_count =
            BooleanClosure::kleeneSquaringCountForLargestComponent(dim);

        for (uint32_t w = 0U; w < options.warmup_runs; ++w) {
            work = base;
            BooleanClosure::transitiveClosureWarshallInPlace(work);
            work = base;
            BooleanClosure::transitiveClosureM4RMSquaringInPlace(
                work,
                squaring_count,
                &scratch,
                m4rm_tables,
                m4rm_opts
            );
        }

        warshall_times.clear();
        m4rm_times.clear();

        for (uint32_t run = 0U; run < measured_runs; ++run) {
            auto measure_warshall = [&]() -> uint64_t {
                work = base;
                const auto t0 = std::chrono::steady_clock::now();
                BooleanClosure::transitiveClosureWarshallInPlace(work);
                const auto t1 = std::chrono::steady_clock::now();
                const auto ns =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                return ns > 0 ? static_cast<uint64_t>(ns) : 1U;
            };

            auto measure_m4rm = [&]() -> uint64_t {
                work = base;
                const auto t0 = std::chrono::steady_clock::now();
                BooleanClosure::transitiveClosureM4RMSquaringInPlace(
                    work,
                    squaring_count,
                    &scratch,
                    m4rm_tables,
                    m4rm_opts
                );
                const auto t1 = std::chrono::steady_clock::now();
                const auto ns =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                return ns > 0 ? static_cast<uint64_t>(ns) : 1U;
            };

            if ((run & 1U) == 0U) {
                warshall_times.push_back(measure_warshall());
                m4rm_times.push_back(measure_m4rm());
            } else {
                m4rm_times.push_back(measure_m4rm());
                warshall_times.push_back(measure_warshall());
            }
        }

        result.samples.push_back(HybridThresholdCalibrationSample{
            .dimension = dim,
            .warshall_median_ns = medianNanoseconds(warshall_times),
            .m4rm_median_ns = medianNanoseconds(m4rm_times),
        });

        if (max_dim - dim < step) {
            break;
        }
        dim += step;
    }

    // Select the threshold cutoff minimizing total normalized runtime regret across all samples.
    std::vector<uint32_t> candidates;
    candidates.reserve(result.samples.size() + 1U);
    if (min_dim > 16U) {
        candidates.push_back(min_dim - 1U);
    } else {
        candidates.push_back(1U);
    }
    for (const HybridThresholdCalibrationSample& sample : result.samples) {
        candidates.push_back(sample.dimension);
    }

    double best_score = std::numeric_limits<double>::infinity();
    uint32_t best_threshold = kInitialDefaultHybridThreshold;

    for (const uint32_t candidate : candidates) {
        double score = 0.0;
        for (const HybridThresholdCalibrationSample& sample : result.samples) {
            const uint64_t best_ns =
                std::max<uint64_t>(1U, std::min(sample.warshall_median_ns, sample.m4rm_median_ns));
            const uint64_t chosen_ns =
                (sample.dimension <= candidate) ? sample.warshall_median_ns : sample.m4rm_median_ns;
            score += static_cast<double>(chosen_ns) / static_cast<double>(best_ns);
        }

        const bool strictly_better = (score + 1e-12 < best_score);
        const bool tied_closer_to_default =
            (std::abs(score - best_score) <= 1e-12) &&
            (absDiffU32(candidate, kInitialDefaultHybridThreshold) <
             absDiffU32(best_threshold, kInitialDefaultHybridThreshold));

        if (strictly_better || tied_closer_to_default) {
            best_score = score;
            best_threshold = candidate;
        }
    }

    result.recommended_threshold = best_threshold;
    if (options.apply_as_process_default) {
        setDefaultHybridWarshallThreshold(result.recommended_threshold);
    }
    return result;
}

}  // namespace hbrick
