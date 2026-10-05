#include "hbrick/bench/process_memory.hpp"

#if defined(__linux__)
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#endif

namespace hbrick {

namespace {

[[nodiscard]] uint64_t readProcStatusKilobytes(const char* key) noexcept {
#if defined(__linux__)
    std::ifstream status_file("/proc/self/status");
    if (!status_file.is_open()) {
        return 0U;
    }

    const std::size_t key_len = std::strlen(key);
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.rfind(key, 0) != 0U) {
            continue;
        }

        const std::size_t value_start = line.find_first_not_of(" \t", key_len);
        if (value_start == std::string::npos) {
            return 0U;
        }

        const std::size_t value_end = line.find_first_of(" \t", value_start);
        const std::string value = line.substr(
            value_start,
            value_end == std::string::npos ? std::string::npos : value_end - value_start
        );

        try {
            return static_cast<uint64_t>(std::stoull(value));
        } catch (...) {
            return 0U;
        }
    }
#else
    (void)key;
#endif
    return 0U;
}

}  // namespace

uint64_t currentProcessRssBytes() noexcept {
    return readProcStatusKilobytes("VmRSS:") * 1024ULL;
}

uint64_t currentProcessHwmBytes() noexcept {
    return readProcStatusKilobytes("VmHWM:") * 1024ULL;
}

}  // namespace hbrick
