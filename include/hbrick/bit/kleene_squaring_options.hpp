/**
 * @file kleene_squaring_options.hpp
 * @ingroup hbrick_bit
 * @brief Execution options for Kleene squaring / boolean matrix power.
 */

#pragma once

#include <cstdint>

namespace hbrick {

/**
 * @brief Strategy for computing reflexive transitive closure.
 * @ingroup hbrick_bit
 */
enum class ClosureAlgorithm : uint8_t {
    /**
     * @brief Hybrid adaptive strategy (Recommended default).
     *
     * Uses 64-way bit-parallel Warshall-Floyd for small matrices (n <= threshold,
     * default 512) for zero table overhead and L1 cache efficiency.
     * Uses Method of Four Russians (M4RM) for larger matrices (n > threshold)
     * for word-parallel streaming and AVX2 bitwise compression.
     */
    Hybrid = 0,

    /**
     * @brief Legacy Gustavson sparse repeated squaring.
     *
     * Iterates over non-zero bits and invokes vector rowOr.
     * Can be forced via HBRICK_CLOSURE_ALGORITHM=gustavson.
     */
    GustavsonSquaring = 1,

    /**
     * @brief Bit-parallel Warshall-Floyd algorithm.
     *
     * Single-pass 64-way bit-parallel pivot closure.
     * Can be forced via HBRICK_CLOSURE_ALGORITHM=warshall.
     */
    Warshall = 2,

    /**
     * @brief 4-Table Method of the Four Russians (M4RM Tetra).
     *
     * 32-bit streaming word parallelism with 4 precomputed tables (L2 cache optimal).
     * Can be forced via HBRICK_CLOSURE_ALGORITHM=m4rm or m4rm4.
     */
    M4RM4Table = 3,

    /**
     * @brief 8-Table Method of the Four Russians (M4RM Octa).
     *
     * 64-bit streaming word parallelism with 8 precomputed tables.
     * Can be forced via HBRICK_CLOSURE_ALGORITHM=m4rm8.
     */
    M4RM8Table = 4
};

/**
 * @brief Controls serial vs multi-threaded Kleene squaring preprocess and algorithm strategy.
 * @ingroup hbrick_bit
 */
struct KleeneSquaringOptions {
    /** @brief When @c true, boolean matrix operations use multiple worker threads. */
    bool use_parallel = false;
    /**
     * @brief Worker count when @ref use_parallel is @c true.
     *
     * @c 1 forces serial. @c 0 selects hardware concurrency (capped).
     */
    uint32_t num_threads = 0U;
    /**
     * @brief When @c false, closure kernels must use direct bit-parallel
     *        repeated squaring instead of SCC-compressing the graph first.
     *
     * Hierarchy-ablation runs disable compression so that a flat closure is
     * compared against the recursive one without an SCC shortcut confounding
     * the difference.
     */
    bool allow_scc_compression = true;

    /**
     * @brief Transitive closure algorithm strategy.
     *
     * Defaults to @ref ClosureAlgorithm::Hybrid.
     * Can be overridden at runtime via @c HBRICK_CLOSURE_ALGORITHM:
     * - "hybrid" (default)
     * - "gustavson" / "legacy" / "squaring"
     * - "warshall"
     * - "m4rm" / "m4rm4"
     * - "m4rm8"
     */
    ClosureAlgorithm algorithm = ClosureAlgorithm::Hybrid;

    /**
     * @brief Dimension threshold for the Hybrid strategy.
     *
     * Matrices with @c n <= hybrid_warshall_threshold use Warshall-Floyd;
     * matrices with @c n > hybrid_warshall_threshold use M4RM.
     * Default is 512. Can be overridden via @c HBRICK_CLOSURE_THRESHOLD.
     */
    uint32_t hybrid_warshall_threshold = 512U;
};

/**
 * @brief Resolves an effective worker count from @p options.
 * @ingroup hbrick_bit
 */
[[nodiscard]] uint32_t resolveKleeneThreadCount(
    const KleeneSquaringOptions& options
) noexcept;

/**
 * @brief Resolves the effective closure algorithm considering options, matrix size, and env vars.
 * @ingroup hbrick_bit
 */
[[nodiscard]] ClosureAlgorithm resolveClosureAlgorithm(
    const KleeneSquaringOptions& options,
    uint32_t num_vertices
) noexcept;

}  // namespace hbrick

