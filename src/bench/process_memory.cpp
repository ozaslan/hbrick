#include "hbrick/bench/process_memory.hpp"

#if defined(__linux__)
#include <cstdint>
#include <fstream>
#include <string>
#endif

namespace hbrick {

uint64_t currentProcessRssBytes() noexcept {
#if defined(__linux__)
    std::ifstream status_file("/proc/self/status");
    if (!status_file.is_open()) {
        return 0U;
    }

    std::string line;
    while (std::getline(status_file, line)) {
        if (line.rfind("VmRSS:", 0) != 0U) {
            continue;
        }

        const std::size_t value_start = line.find_first_not_of(" \t", 6U);
        if (value_start == std::string::npos) {
            return 0U;
        }

        const std::size_t value_end = line.find_first_of(" \t", value_start);
        const std::string value = line.substr(
            value_start,
            value_end == std::string::npos ? std::string::npos : value_end - value_start
        );

        try {
            const uint64_t kilobytes = static_cast<uint64_t>(std::stoull(value));
            return kilobytes * 1024ULL;
        } catch (...) {
            return 0U;
        }
    }
#endif
    return 0U;
}

}  // namespace hbrick
