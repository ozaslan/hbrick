/**
 * @file kleene_squaring_options.hpp
 * @ingroup hbrick_bit
 * @brief Execution options for Kleene squaring / boolean matrix power.
 */

#pragma once

#include <cstdint>

namespace hbrick {

/**
 * @brief Controls serial vs multi-threaded Kleene squaring preprocess.
 * @ingroup hbrick_bit
 */
struct KleeneSquaringOptions {
    /** @brief When @c true, boolean matrix squaring uses multiple worker threads. */
    bool use_parallel = false;
    /**
     * @brief Worker count when @ref use_parallel is @c true.
     *
     * @c 1 forces serial. @c 0 selects hardware concurrency (capped).
     */
    uint32_t num_threads = 0U;
};

/**
 * @brief Resolves an effective worker count from @p options.
 * @ingroup hbrick_bit
 */
[[nodiscard]] uint32_t resolveKleeneThreadCount(
    const KleeneSquaringOptions& options
) noexcept;

}  // namespace hbrick
