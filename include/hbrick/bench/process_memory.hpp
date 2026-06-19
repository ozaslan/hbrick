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

}  // namespace hbrick
