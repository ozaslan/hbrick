/**
 * @file hbrick_query_scratch.hpp
 * @ingroup hbrick_baselines
 * @brief Reusable bit-vector workspace for hierarchical H-BRICK queries.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/bit/bit_vector.hpp"

namespace hbrick {

class HBrickIndex;

/**
 * @brief Preallocated boolean vectors for @ref HBrickBaseline::query.
 * @ingroup hbrick_baselines
 *
 * Call @ref prepare once after preprocessing so query avoids heap allocation.
 */
class HBrickQueryScratch {
public:
    /**
     * @brief Sizes internal buffers for the built @p index.
     * @ingroup hbrick_baselines
     */
    void prepare(const HBrickIndex& index);

    /** @brief Clears all prepared vectors to zero. @ingroup hbrick_baselines */
    void clearVectors() noexcept;

    /** @brief Source-side vectors indexed by hierarchy level. @ingroup hbrick_baselines */
    [[nodiscard]] std::vector<BitVector>& sourceChain() noexcept { return source_chain_; }
    /** @brief Target-side vectors indexed by hierarchy level. @ingroup hbrick_baselines */
    [[nodiscard]] std::vector<BitVector>& targetChain() noexcept { return target_chain_; }
    /** @brief Temporary gamma workspace A. @ingroup hbrick_baselines */
    [[nodiscard]] BitVector& gammaA() noexcept { return gamma_a_; }
    /** @brief Temporary gamma workspace B. @ingroup hbrick_baselines */
    [[nodiscard]] BitVector& gammaB() noexcept { return gamma_b_; }
    /** @brief Temporary gamma workspace C. @ingroup hbrick_baselines */
    [[nodiscard]] BitVector& gammaC() noexcept { return gamma_c_; }

private:
    std::vector<BitVector> source_chain_{};
    std::vector<BitVector> target_chain_{};
    BitVector gamma_a_{};
    BitVector gamma_b_{};
    BitVector gamma_c_{};
};

}  // namespace hbrick
