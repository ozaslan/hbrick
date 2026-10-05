/**
 * @file hbrick_hierarchy_query.hpp
 * @ingroup hbrick_baselines
 * @brief Shared allocation-free kernels for hierarchical H-BRICK query lifts and meets.
 */

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <span>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/bit/bit_vector.hpp"
#include "hbrick/tile/fused_tree_lifts.hpp"
#include "hbrick/tile/hbrick_index.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/region_node.hpp"
#include "hbrick/tile/super_tile_summary.hpp"

namespace hbrick {

inline void embedChildPorts(
    const BitVector& child,
    const std::span<const uint32_t> child_port_to_gamma,
    BitVector& out_gamma
) noexcept {
    out_gamma.clear();
    const uint32_t port_limit = std::min(
        static_cast<uint32_t>(child_port_to_gamma.size()),
        static_cast<uint32_t>(child.numBits())
    );
    if (port_limit == 0U) {
        return;
    }

    const uint64_t* words = child.wordsData();
    const size_t num_words = child.numWords();
    for (size_t word_index = 0U; word_index < num_words; ++word_index) {
        uint64_t word = words[word_index];
        if (word == 0U) {
            continue;
        }

        const size_t bit_pos_base = word_index * 64U;
        if (bit_pos_base >= static_cast<size_t>(port_limit)) {
            break;
        }

        do {
            const unsigned bit = static_cast<unsigned>(std::countr_zero(word));
            const uint32_t child_port =
                static_cast<uint32_t>(bit_pos_base + static_cast<size_t>(bit));
            word &= word - 1U;
            if (child_port >= port_limit) {
                continue;
            }
            const uint32_t gamma_index = child_port_to_gamma[child_port];
            if (gamma_index != std::numeric_limits<uint32_t>::max()) {
                out_gamma.set(gamma_index);
            }
        } while (word != 0U);
    }
}

inline void multiplyVectorClosure(
    const BitVector& input,
    const BitMatrix& closure,
    BitVector& output
) noexcept {
    output.clear();
    const uint32_t row_limit =
        std::min(closure.numRows(), static_cast<uint32_t>(input.numBits()));
    if (row_limit == 0U) {
        return;
    }

    const uint64_t* words = input.wordsData();
    const size_t num_words = input.numWords();
    for (size_t word_index = 0U; word_index < num_words; ++word_index) {
        uint64_t word = words[word_index];
        if (word == 0U) {
            continue;
        }

        const size_t bit_pos_base = word_index * 64U;
        if (bit_pos_base >= static_cast<size_t>(row_limit)) {
            break;
        }

        do {
            const unsigned bit = static_cast<unsigned>(std::countr_zero(word));
            const uint32_t row =
                static_cast<uint32_t>(bit_pos_base + static_cast<size_t>(bit));
            word &= word - 1U;
            if (row >= row_limit) {
                continue;
            }
            output.rowOrFrom(closure.row(row));
        } while (word != 0U);
    }
}

inline void projectToExterior(
    const BitVector& gamma,
    const std::span<const uint32_t> exterior_gamma_indices,
    BitVector& exterior
) noexcept {
    exterior.clear();
    for (uint32_t exterior_index = 0U; exterior_index < exterior_gamma_indices.size();
         ++exterior_index) {
        if (gamma.test(exterior_gamma_indices[exterior_index])) {
            exterior.set(exterior_index);
        }
    }
}

[[nodiscard]] inline bool vectorClosureIntersects(
    const BitVector& vector,
    const BitMatrix& closure,
    const BitVector& other
) noexcept {
    const uint32_t row_limit = closure.numRows();
    const size_t num_words = vector.numWords();

    struct NonZeroWord {
        uint32_t index = 0U;
        uint64_t mask = 0U;
    };
    std::array<NonZeroWord, 8> sparse_words{};
    size_t sparse_count = 0U;
    bool is_sparse = true;

    const size_t other_num_words = other.numWords();
    for (size_t w = 0U; w < other_num_words; ++w) {
        const uint64_t w_val = other.word(w);
        if (w_val != 0U) {
            if (sparse_count < sparse_words.size()) {
                sparse_words[sparse_count++] = {static_cast<uint32_t>(w), w_val};
            } else {
                is_sparse = false;
                break;
            }
        }
    }

    auto rowIntersectsOther = [&](const BitVector& row_vec) noexcept -> bool {
        if (is_sparse) {
            const size_t row_words = row_vec.numWords();
            for (size_t s = 0U; s < sparse_count; ++s) {
                const uint32_t w_idx = sparse_words[s].index;
                if (w_idx < row_words && (row_vec.word(w_idx) & sparse_words[s].mask) != 0U) {
                    return true;
                }
            }
            return false;
        }
        return row_vec.intersects(other);
    };

    const uint64_t* words = vector.wordsData();
    for (size_t word_index = 0U; word_index < num_words; ++word_index) {
        uint64_t word = words[word_index];
        if (word == 0U) {
            continue;
        }

        const size_t bit_pos_base = word_index * 64U;
        if (bit_pos_base >= static_cast<size_t>(row_limit)) {
            break;
        }

        do {
            const unsigned bit = static_cast<unsigned>(std::countr_zero(word));
            const uint32_t row =
                static_cast<uint32_t>(bit_pos_base + static_cast<size_t>(bit));
            word &= word - 1U;
            if (row < row_limit && rowIntersectsOther(closure.row(row))) {
                return true;
            }
        } while (word != 0U);
    }
    return false;
}

[[nodiscard]] inline bool bilinearMeet(
    const BitVector& left_gamma,
    const BitMatrix& closure,
    const BitVector& right_gamma,
    const BitMatrix& closure_transpose
) noexcept {
    if (!left_gamma.any() || !right_gamma.any()) {
        return false;
    }
    if (left_gamma.intersects(right_gamma)) {
        return true;
    }

    const size_t left_pop = left_gamma.popcount();
    const size_t right_pop = right_gamma.popcount();
    if (left_pop <= right_pop) {
        return vectorClosureIntersects(left_gamma, closure, right_gamma);
    }
    return vectorClosureIntersects(right_gamma, closure_transpose, left_gamma);
}

[[nodiscard]] inline uint32_t childEmbeddingIndex(
    const RegionNode& child,
    const SuperTileSummary& parent_summary
) noexcept {
    const uint32_t child_slot = child.child_slot_in_parent;
    if (child_slot >= parent_summary.child_embedding_of.size()) {
        return std::numeric_limits<uint32_t>::max();
    }
    const uint32_t embedding_index = parent_summary.child_embedding_of[child_slot];
    if (embedding_index == UINT32_MAX
        || embedding_index >= parent_summary.child_port_to_gamma.size()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return embedding_index;
}

/**
 * @brief True iff a closed forward Gamma label meets an embedded reverse port set.
 *
 * Iterates set bits of @p reverse_gamma (the unclosed \(Jy\) side) and tests
 * @p forward_gamma. That side is typically sparse; a dense–dense fallback uses
 * a word-wise AND.
 */
[[nodiscard]] inline bool sparseLabelIntersect(
    const BitVector& forward_gamma,
    const BitVector& reverse_gamma
) noexcept {
    if (!reverse_gamma.any() || !forward_gamma.any()) {
        return false;
    }

    const uint32_t forward_bits = static_cast<uint32_t>(forward_gamma.numBits());
    const uint64_t* words = reverse_gamma.wordsData();
    const size_t num_words = reverse_gamma.numWords();
    size_t nonzero_words = 0U;
    for (size_t w = 0U; w < num_words; ++w) {
        nonzero_words += static_cast<size_t>(words[w] != 0U);
        if (nonzero_words > 8U) {
            return forward_gamma.intersects(reverse_gamma);
        }
    }

    for (size_t w = 0U; w < num_words; ++w) {
        uint64_t word = words[w];
        while (word != 0U) {
            const unsigned bit = static_cast<unsigned>(std::countr_zero(word));
            word &= word - 1U;
            const uint32_t index = static_cast<uint32_t>(w * 64U + bit);
            if (index < forward_bits && forward_gamma.test(index)) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Lifts one ancestor-chain step with the fused rectangular operator.
 *
 * Clears @p parent_vec when the child vector is empty, the parent summary is
 * incomplete, or the child embedding is missing — the same write-through
 * contract used by lazy endpoint caches.
 */
inline void fusedLiftOneStep(
    const HBrickIndex& index,
    const FusedTreeLifts& fused_lifts,
    const std::span<const RegionNodeId> chain,
    const uint32_t chain_index,
    const bool source_side,
    BitVector& child_vec,
    BitVector& parent_vec
) noexcept {
    if (chain_index + 1U >= chain.size() || !child_vec.any()) {
        parent_vec.clear();
        return;
    }
    const HierarchyTree& hierarchy = index.hierarchy();
    const RegionNodeId child_id = chain[chain_index];
    const RegionNodeId parent_id = chain[chain_index + 1U];
    const RegionNode& child_node = hierarchy.node(child_id.level, child_id.index);
    const SuperTileSummary& parent_sum =
        index.superSummary(parent_id.level, parent_id.index);
    if (parent_sum.status != BaselineStatus::Completed) {
        parent_vec.clear();
        return;
    }
    const uint32_t emb_idx = childEmbeddingIndex(child_node, parent_sum);
    if (emb_idx == std::numeric_limits<uint32_t>::max()) {
        parent_vec.clear();
        return;
    }
    if (source_side) {
        fused_lifts.liftSource(
            parent_id.level,
            parent_id.index,
            emb_idx,
            child_vec,
            parent_vec
        );
    } else {
        fused_lifts.liftTarget(
            parent_id.level,
            parent_id.index,
            emb_idx,
            child_vec,
            parent_vec
        );
    }
}

}  // namespace hbrick
