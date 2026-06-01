#include "hbrick/core/status_reporting.hpp"

namespace hbrick {

std::string_view toString(ReachabilityAnswer answer) noexcept {
    switch (answer) {
        case ReachabilityAnswer::Unreachable:
            return "Unreachable";
        case ReachabilityAnswer::Reachable:
            return "Reachable";
    }
    return "UnknownReachabilityAnswer";
}

std::string_view toString(BaselineStatus status) noexcept {
    switch (status) {
        case BaselineStatus::NotRun:
            return "NotRun";
        case BaselineStatus::Completed:
            return "Completed";
        case BaselineStatus::SkippedByPolicy:
            return "SkippedByPolicy";
        case BaselineStatus::OutOfMemory:
            return "OutOfMemory";
        case BaselineStatus::Failed:
            return "Failed";
    }
    return "UnknownBaselineStatus";
}

}  // namespace hbrick
