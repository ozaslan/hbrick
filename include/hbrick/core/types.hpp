#pragma once

#include <cstdint>

namespace hbrick {

enum class ReachabilityAnswer : uint8_t {
    Unreachable = 0,
    Reachable = 1
};

enum class BaselineStatus : uint8_t {
    NotRun = 0,
    Completed,
    SkippedByPolicy,
    OutOfMemory,
    Failed
};

}  // namespace hbrick
