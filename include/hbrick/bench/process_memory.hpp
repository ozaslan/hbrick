/**
 * @file process_memory.hpp
 * @ingroup hbrick_bench
 * @brief Process resident-set size sampling for benchmark memory metrics.
 */

#pragma once

#include <cstdint>

namespace hbrick {

/**
 * @brief Returns the current process resident-set size in bytes.
 * @ingroup hbrick_bench
 *
 * On platforms without RSS support the function returns @c 0.
 */
[[nodiscard]] uint64_t currentProcessRssBytes() noexcept;

/**
 * @brief Returns the process high-water-mark resident set in bytes (@c VmHWM).
 * @ingroup hbrick_bench
 *
 * On platforms without HWM support the function returns @c 0.
 */
[[nodiscard]] uint64_t currentProcessHwmBytes() noexcept;

}  // namespace hbrick
