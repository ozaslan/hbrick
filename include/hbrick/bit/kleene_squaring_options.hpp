/**
 * @file kleene_squaring_options.hpp
 * @ingroup hbrick_bit
 * @brief Execution options for Kleene squaring / boolean matrix power.
 */

#pragma once

#include <cstdint>
#include <vector>

namespace hbrick {

/**
 * @brief Returns the process-wide default Warshall/M4RM crossover dimension (512 unless updated).
 * @ingroup hbrick_bit
 *
 * @return Current process-wide default crossover dimension @c theta_hyb.
 */
[[nodiscard]] uint32_t defaultHybridWarshallThreshold() noexcept;

/**
 * @brief Updates the process-wide default Warshall/M4RM crossover dimension used by newly
 *        default-constructed @ref KleeneSquaringOptions instances.
 * @ingroup hbrick_bit
 *
 * @param threshold Crossover dimension @c theta_hyb (clamped to at least @c 1).
 */
void setDefaultHybridWarshallThreshold(uint32_t threshold) noexcept;

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
     * Initialized from @ref defaultHybridWarshallThreshold (512 unless calibrated).
     * Can also be overridden at runtime via @c HBRICK_CLOSURE_THRESHOLD.
     */
    uint32_t hybrid_warshall_threshold = defaultHybridWarshallThreshold();
};

/**
 * @brief Configuration for host-specific calibration of @ref KleeneSquaringOptions::hybrid_warshall_threshold.
 * @ingroup hbrick_bit
 */
struct HybridThresholdCalibrationOptions {
    /** @brief Smallest square relation dimension tested (clamped to >= 16). */
    uint32_t min_dimension = 64U;
    /** @brief Largest square relation dimension tested. */
    uint32_t max_dimension = 1024U;
    /** @brief Resolution step between consecutive candidate dimensions (e.g. 32 tests 64, 96, 128, ..., 384, ..., 512, ...). */
    uint32_t dimension_step = 32U;
    /** @brief Unmeasured warmup runs per kernel at each candidate dimension. */
    uint32_t warmup_runs = 1U;
    /** @brief Measured runs per kernel at each candidate dimension (median runtime is recorded). */
    uint32_t measured_runs = 3U;
    /** @brief Base execution options controlling serial vs. multi-threaded M4RM dispatch during calibration. */
    KleeneSquaringOptions base_options{};
    /** @brief Deterministic PRNG seed for synthetic 2D-lattice test relations. */
    uint64_t seed = 0x48425249434BULL;
    /** @brief When @c true, updates @ref setDefaultHybridWarshallThreshold with the calibrated result. */
    bool apply_as_process_default = false;
};

/**
 * @brief Timing measurement at one candidate relation dimension during threshold calibration.
 * @ingroup hbrick_bit
 */
struct HybridThresholdCalibrationSample {
    /** @brief Square relation dimension @c d evaluated. */
    uint32_t dimension = 0U;
    /** @brief Median execution time of 64-way bit-parallel Warshall in nanoseconds. */
    uint64_t warshall_median_ns = 0U;
    /** @brief Median execution time of M4RM repeated squaring in nanoseconds. */
    uint64_t m4rm_median_ns = 0U;
};

/**
 * @brief Result of sweeping Warshall versus M4RM across candidate dimensions on the current host.
 * @ingroup hbrick_bit
 */
struct HybridThresholdCalibrationResult {
    /** @brief Empirically selected crossover dimension @c theta_hyb minimizing normalized runtime regret. */
    uint32_t recommended_threshold = 512U;
    /** @brief Per-dimension timing curve across all evaluated candidate dimensions. */
    std::vector<HybridThresholdCalibrationSample> samples;
};

/**
 * @brief Benchmarks 64-way bit-parallel Warshall against M4RM repeated squaring across a fine-grained
 *        dimension grid and returns the optimal @ref KleeneSquaringOptions::hybrid_warshall_threshold.
 * @ingroup hbrick_bit
 *
 * Cold-path calibration routine intended to be called once on a target machine (e.g. at startup
 * or deployment) so that the closure backend adapts to host L1/L2 cache capacity and SIMD width.
 *
 * @param options Sweep bounds, step resolution, repetition counts, and threading configuration.
 * @return Calibration result containing @c recommended_threshold and per-dimension median timings.
 */
[[nodiscard]] HybridThresholdCalibrationResult calibrateHybridWarshallThreshold(
    const HybridThresholdCalibrationOptions& options = {}
);

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

