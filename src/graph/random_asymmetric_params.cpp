#include "hbrick/graph/random_asymmetric_params.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace hbrick {

namespace {

[[nodiscard]] long double clampProbability(const long double value) noexcept {
    if (std::isnan(value) || value < 0.0L) {
        return 0.0L;
    }
    if (value > 1.0L) {
        return 1.0L;
    }
    return value;
}

}  // namespace

RandomAsymmetricParams sanitizeRandomAsymmetricParams(
    RandomAsymmetricParams params
) noexcept {
    params.p_bidirectional = clampProbability(params.p_bidirectional);
    params.p_one_way = clampProbability(params.p_one_way);
    params.p_against_gradient = clampProbability(params.p_against_gradient);
    if (std::isnan(params.p_edge_drop) || params.p_edge_drop < 0.0L) {
        params.p_edge_drop = -1.0L;
    } else {
        params.p_edge_drop = clampProbability(params.p_edge_drop);
    }

    const long double orientation_sum = params.p_bidirectional + params.p_one_way;
    if (orientation_sum > 1.0L) {
        const long double scale = 1.0L / orientation_sum;
        params.p_bidirectional *= scale;
        params.p_one_way *= scale;
    }

    if (std::isnan(params.gradient_angle_degrees)) {
        params.gradient_angle_degrees = 45.0;
    }

    return params;
}

RandomAsymmetricParams resolveRandomAsymmetricMix(
    RandomAsymmetricParams params
) noexcept {
    params = sanitizeRandomAsymmetricParams(params);
    if (params.p_edge_drop < 0.0L) {
        return params;
    }

    long double sum =
        params.p_bidirectional + params.p_one_way + params.p_edge_drop;
    if (sum > 1.0L) {
        const long double scale = 1.0L / sum;
        params.p_bidirectional *= scale;
        params.p_one_way *= scale;
        params.p_edge_drop *= scale;
        return params;
    }
    params.p_bidirectional += 1.0L - sum;
    params.p_edge_drop = 0.0L;
    return params;
}

long double expectedDirectedArcsPerPair(
    const RandomAsymmetricParams& params
) noexcept {
    const RandomAsymmetricParams resolved = resolveRandomAsymmetricMix(params);
    return 2.0L * resolved.p_bidirectional + resolved.p_one_way;
}

uint64_t expectedDirectedArcCount(
    const uint64_t undirected_pairs,
    const RandomAsymmetricParams& params
) noexcept {
    const long double mean =
        static_cast<long double>(undirected_pairs) * expectedDirectedArcsPerPair(params);
    if (mean <= 0.0L) {
        return 0U;
    }
    return static_cast<uint64_t>(mean + 0.5L);
}

bool randomAsymmetricArcCountMatchesExpectation(
    const uint64_t observed_arcs,
    const uint64_t undirected_pairs,
    const RandomAsymmetricParams& params,
    std::string& error_message
) {
    const uint64_t expected = expectedDirectedArcCount(undirected_pairs, params);
    const double pairs = static_cast<double>(undirected_pairs);
    const double tolerance = std::max(16.0, 6.0 * std::sqrt(std::max(pairs, 1.0)));
    const double delta = observed_arcs >= expected
        ? static_cast<double>(observed_arcs - expected)
        : static_cast<double>(expected - observed_arcs);
    if (delta <= tolerance) {
        return true;
    }

    error_message =
        "Random-asymmetric arc count mismatch: observed E="
        + std::to_string(observed_arcs) + ", expected about "
        + std::to_string(expected) + " from P="
        + std::to_string(undirected_pairs) + " undirected pairs (mean arcs/pair="
        + std::to_string(static_cast<double>(expectedDirectedArcsPerPair(params)))
        + "). p_one_way is the one-way fraction of adjacencies, not an edge-drop rate.";
    return false;
}

RandomAsymmetricParams completeRandomAsymmetricOrientation(
    RandomAsymmetricParams params
) noexcept {
    params = sanitizeRandomAsymmetricParams(params);
    params.p_edge_drop = 0.0L;
    constexpr long double kEps = 1.0e-9L;
    if (params.p_bidirectional <= kEps && params.p_one_way > kEps) {
        params.p_bidirectional = 1.0L - params.p_one_way;
    } else {
        params.p_one_way = 1.0L - params.p_bidirectional;
    }
    return params;
}

bool randomAsymmetricOrientationIsComplete(
    const RandomAsymmetricParams& params,
    const long double epsilon
) noexcept {
    if (params.p_edge_drop > epsilon) {
        return false;
    }
    const long double sum = params.p_bidirectional + params.p_one_way;
    const long double delta = sum >= 1.0L ? sum - 1.0L : 1.0L - sum;
    return delta <= epsilon;
}

bool enforceCompleteRandomAsymmetricOrientation(
    RandomAsymmetricParams& params,
    std::string& error_message
) {
    if (!randomAsymmetricOrientationIsComplete(params)) {
        error_message =
            "Random-asymmetric orientation must satisfy |p_one + p_bi - 1| <= 1e-6 "
            "(got p_one="
            + std::to_string(static_cast<double>(params.p_one_way)) + ", p_bi="
            + std::to_string(static_cast<double>(params.p_bidirectional)) + ")";
        return false;
    }
    params = completeRandomAsymmetricOrientation(params);
    return true;
}

}  // namespace hbrick
