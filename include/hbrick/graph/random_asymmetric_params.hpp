#pragma once

#include <cstdint>

namespace hbrick {

struct RandomAsymmetricParams {
    uint64_t seed = 0;
    long double p_bidirectional = 0.0L;
    long double p_one_way = 0.0L;
};

enum class GridEdgeConversionMode : uint8_t {
    RandomAsymmetric = 0,
    BidirectionalAll,
    AcyclicEastSouth
};

}  // namespace hbrick
