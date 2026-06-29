#include "hbrick/graph/random_asymmetric_params.hpp"

namespace hbrick {

namespace {

[[nodiscard]] long double clampProbability(const long double value) noexcept {
    if (value != value || value < 0.0L) {
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

    const long double orientation_sum = params.p_bidirectional + params.p_one_way;
    if (orientation_sum > 1.0L) {
        const long double scale = 1.0L / orientation_sum;
        params.p_bidirectional *= scale;
        params.p_one_way *= scale;
    }

    if (params.gradient_angle_degrees != params.gradient_angle_degrees) {
        params.gradient_angle_degrees = 45.0;
    }

    return params;
}

}  // namespace hbrick
