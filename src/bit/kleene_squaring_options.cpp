#include "hbrick/bit/kleene_squaring_options.hpp"

#include <algorithm>
#include <thread>

namespace hbrick {

namespace {

constexpr uint32_t kMaxKleeneThreads = 64U;

}  // namespace

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

}  // namespace hbrick
