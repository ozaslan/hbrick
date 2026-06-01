#pragma once

#include <cstdint>
#include <string>

namespace hbrick {

struct BenchSample {
    std::string name;
    uint64_t iterations = 0;
    uint64_t elapsed_nanoseconds = 0;

    [[nodiscard]] double nanosecondsPerIteration() const noexcept;
};

}  // namespace hbrick
