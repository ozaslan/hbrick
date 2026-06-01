#pragma once

#include <cstdint>

namespace hbrick {

class BenchTimer {
public:
    void start() noexcept;
    void stop() noexcept;

    [[nodiscard]] uint64_t elapsedNanoseconds() const noexcept;

private:
    uint64_t start_nanoseconds_ = 0;
    uint64_t elapsed_nanoseconds_ = 0;
    bool running_ = false;
};

}  // namespace hbrick
