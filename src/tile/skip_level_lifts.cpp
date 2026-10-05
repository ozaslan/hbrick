/**
 * @file skip_level_lifts.cpp
 * @brief Boolean products of fused one-hop lifts along ancestor chains.
 */

#include "hbrick/tile/skip_level_lifts.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/tile/hbrick_index.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/region_node.hpp"
#include "hbrick/tile/super_tile_summary.hpp"

namespace hbrick {

namespace {

[[nodiscard]] uint32_t childEmbeddingIndex(
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

[[nodiscard]] BitMatrix booleanRowProduct(
    const BitMatrix& left,
    const BitMatrix& right
) {
    BitMatrix out(left.numRows(), right.numCols());
    const uint32_t join = std::min(left.numCols(), right.numRows());
    if (join == 0U || left.numRows() == 0U || right.numCols() == 0U) {
        return out;
    }

    for (uint32_t row_index = 0U; row_index < left.numRows(); ++row_index) {
        const BitVector& row = left.row(row_index);
        const uint64_t* words = row.wordsData();
        const size_t num_words = row.numWords();
        for (size_t w = 0U; w < num_words; ++w) {
            uint64_t word = words[w];
            if (word == 0U) {
                continue;
            }
            const size_t bit_base = w * 64U;
            if (bit_base >= static_cast<size_t>(join)) {
                break;
            }
            do {
                const unsigned bit = static_cast<unsigned>(std::countr_zero(word));
                const uint32_t col = static_cast<uint32_t>(bit_base + bit);
                word &= word - 1U;
                if (col < join) {
                    out.row(row_index).rowOrFrom(right.row(col));
                }
            } while (word != 0U);
        }
    }
    return out;
}

struct M4rmWorkspace {
    std::vector<uint64_t> words;
};

[[nodiscard]] BitMatrix booleanProductM4RM(
    const BitMatrix& left,
    const BitMatrix& right,
    M4rmWorkspace& workspace
) {
    const uint32_t m = left.numRows();
    const uint32_t k = std::min(left.numCols(), right.numRows());
    const uint32_t n = right.numCols();
    BitMatrix out(m, n);
    if (m == 0U || k == 0U || n == 0U) {
        return out;
    }

    const size_t words_per_row = (static_cast<size_t>(n) + 63U) / 64U;
    constexpr size_t kTables = 4U;
    constexpr size_t kEntries = 256U;
    const size_t required = kTables * kEntries * words_per_row;
    if (workspace.words.size() < required) {
        workspace.words.resize(required);
    }

    auto get_table_row = [&](const size_t t, const size_t val) -> uint64_t* {
        return workspace.words.data() + (t * kEntries + val) * words_per_row;
    };

    for (uint32_t k_base = 0U; k_base < k; k_base += 32U) {
        const uint32_t active = std::min(32U, k - k_base);

        for (size_t t = 0U; t < kTables; ++t) {
            uint64_t* t0 = get_table_row(t, 0U);
            std::memset(t0, 0, words_per_row * sizeof(uint64_t));

            const uint32_t t_base = static_cast<uint32_t>(t * 8U);
            const uint32_t t_bits = (active > t_base) ? std::min(8U, active - t_base) : 0U;
            const size_t num_vals = size_t{1} << t_bits;

            for (size_t bit = 0U; bit < t_bits; ++bit) {
                const uint32_t right_row = k_base + t_base + static_cast<uint32_t>(bit);
                const uint64_t* src_row = right.row(right_row).wordsData();
                const size_t bit_mask = size_t{1} << bit;

                for (size_t val = 0U; val < bit_mask; ++val) {
                    const uint64_t* prev = get_table_row(t, val);
                    uint64_t* curr = get_table_row(t, val | bit_mask);

                    size_t w = 0U;
#if defined(__AVX2__)
                    for (; w + 4U <= words_per_row; w += 4U) {
                        __m256i p = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&prev[w]));
                        __m256i s = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&src_row[w]));
                        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&curr[w]), _mm256_or_si256(p, s));
                    }
#endif
                    for (; w < words_per_row; ++w) {
                        curr[w] = prev[w] | src_row[w];
                    }
                }
            }
            if (num_vals < kEntries) {
                std::memset(
                    get_table_row(t, num_vals),
                    0,
                    (kEntries - num_vals) * words_per_row * sizeof(uint64_t)
                );
            }
        }

        const uint32_t left_word_index = k_base / 64U;
        const bool is_high_word = (k_base % 64U) != 0U;
        const uint32_t active_mask = (active >= 32U) ? 0xFFFFFFFFU : ((1U << active) - 1U);

        for (uint32_t r = 0U; r < m; ++r) {
            const uint64_t* left_words = left.row(r).wordsData();
            uint64_t word = (left_word_index < left.row(r).numWords())
                ? left_words[left_word_index]
                : 0ULL;
            if (is_high_word) {
                word >>= 32U;
            }
            uint32_t chunk = static_cast<uint32_t>(word & 0xFFFFFFFFULL) & active_mask;
            if (chunk == 0U) {
                continue;
            }

            const uint8_t v0 = static_cast<uint8_t>(chunk & 0xFFU);
            const uint8_t v1 = static_cast<uint8_t>((chunk >> 8U) & 0xFFU);
            const uint8_t v2 = static_cast<uint8_t>((chunk >> 16U) & 0xFFU);
            const uint8_t v3 = static_cast<uint8_t>((chunk >> 24U) & 0xFFU);

            const uint64_t* t0 = get_table_row(0U, v0);
            const uint64_t* t1 = get_table_row(1U, v1);
            const uint64_t* t2 = get_table_row(2U, v2);
            const uint64_t* t3 = get_table_row(3U, v3);

            uint64_t* out_words = out.row(r).wordsData();

            size_t w = 0U;
#if defined(__AVX2__)
            for (; w + 4U <= words_per_row; w += 4U) {
                __m256i o = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&out_words[w]));
                __m256i m0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&t0[w]));
                __m256i m1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&t1[w]));
                __m256i m2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&t2[w]));
                __m256i m3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&t3[w]));
                __m256i comb = _mm256_or_si256(_mm256_or_si256(m0, m1), _mm256_or_si256(m2, m3));
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(&out_words[w]), _mm256_or_si256(o, comb));
            }
#endif
            for (; w < words_per_row; ++w) {
                out_words[w] |= (t0[w] | t1[w] | t2[w] | t3[w]);
            }
        }
    }
    return out;
}

[[nodiscard]] BitMatrix booleanRowProductAdaptive(
    const BitMatrix& left,
    const BitMatrix& right,
    M4rmWorkspace& workspace
) {
    const uint32_t m = left.numRows();
    const uint32_t k = std::min(left.numCols(), right.numRows());
    const uint32_t n = right.numCols();

    if (n == 0U || m < 24U || k < 64U || n < 16U) {
        return booleanRowProduct(left, right);
    }

    const double p =
        static_cast<double>(left.popcount()) /
        static_cast<double>(static_cast<uint64_t>(m) * k);

    if (p <= 0.125) {
        return booleanRowProduct(left, right);
    }

    if ((p >= 0.30 && m >= 32U) ||
        ((8.0 * p - 1.0) * static_cast<double>(m) > 256.0)) {
        return booleanProductM4RM(left, right, workspace);
    }

    return booleanRowProduct(left, right);
}

void applyLiftRows(
    const BitMatrix& lift,
    const BitVector& input,
    BitVector& out_parent
) noexcept {
    out_parent.clear();
    const uint32_t row_limit = lift.numRows();
    const uint64_t* words = input.wordsData();
    const size_t num_words = input.numWords();
    for (size_t w = 0U; w < num_words; ++w) {
        uint64_t word = words[w];
        if (word == 0U) {
            continue;
        }
        const size_t bit_base = w * 64U;
        if (bit_base >= row_limit) {
            break;
        }
        do {
            const unsigned bit = static_cast<unsigned>(std::countr_zero(word));
            const uint32_t row = static_cast<uint32_t>(bit_base + bit);
            word &= word - 1U;
            if (row < row_limit) {
                out_parent.rowOrFrom(lift.row(row));
            }
        } while (word != 0U);
    }
}

}  // namespace

SkipLevelLifts SkipLevelLifts::build(
    const HBrickIndex& index,
    const FusedTreeLifts& fused_lifts
) {
    SkipLevelLifts result;
    if (index.status() != BaselineStatus::Completed || !fused_lifts.isValid()) {
        return result;
    }

    const HierarchyTree& hierarchy = index.hierarchy();
    const uint32_t num_tiles =
        static_cast<uint32_t>(index.brickIndex().tiles().summaries().size());
    result.tiles_.resize(num_tiles);

    M4rmWorkspace workspace;

    for (uint32_t tile = 0U; tile < num_tiles; ++tile) {
        const std::span<const RegionNodeId> chain = hierarchy.ancestorChain(tile);
        SkipTileLifts& slot = result.tiles_[tile];
        slot.by_level.resize(chain.size());
        if (chain.size() <= 1U) {
            continue;
        }

        for (uint32_t k = 1U; k < chain.size(); ++k) {
            const RegionNodeId child_id = chain[k - 1U];
            const RegionNodeId parent_id = chain[k];
            const RegionNode& child_node = hierarchy.node(child_id.level, child_id.index);
            const SuperTileSummary& parent_summary =
                index.superSummary(parent_id.level, parent_id.index);
            if (parent_summary.status != BaselineStatus::Completed) {
                break;
            }
            const uint32_t emb_idx = childEmbeddingIndex(child_node, parent_summary);
            if (emb_idx == std::numeric_limits<uint32_t>::max()) {
                break;
            }
            const FusedChildLift* hop =
                fused_lifts.childLift(parent_id.level, parent_id.index, emb_idx);
            if (hop == nullptr || hop->forward.numRows() == 0U) {
                break;
            }

            if (k == 1U) {
                slot.by_level[k] = *hop;
                continue;
            }

            const FusedChildLift& prev = slot.by_level[k - 1U];
            if (prev.forward.numRows() == 0U) {
                break;
            }
            slot.by_level[k].forward =
                booleanRowProductAdaptive(prev.forward, hop->forward, workspace);
            slot.by_level[k].reverse_transpose =
                booleanRowProductAdaptive(
                    prev.reverse_transpose,
                    hop->reverse_transpose,
                    workspace
                );
        }
    }

    result.is_valid_ = true;
    return result;
}

uint64_t SkipLevelLifts::memoryBytes() const noexcept {
    uint64_t total = sizeof(SkipLevelLifts);
    total += tiles_.capacity() * sizeof(SkipTileLifts);
    for (const SkipTileLifts& tile : tiles_) {
        total += tile.by_level.capacity() * sizeof(FusedChildLift);
        for (const FusedChildLift& lift : tile.by_level) {
            total += lift.forward.memoryBytes();
            total += lift.reverse_transpose.memoryBytes();
        }
    }
    return total;
}

void SkipLevelLifts::liftSource(
    const uint32_t base_tile,
    const uint32_t dest_level,
    const BitVector& leaf_vector,
    BitVector& out_parent
) const noexcept {
    out_parent.clear();
    if (base_tile >= tiles_.size() || dest_level == 0U
        || dest_level >= tiles_[base_tile].by_level.size()) {
        return;
    }
    applyLiftRows(tiles_[base_tile].by_level[dest_level].forward, leaf_vector, out_parent);
}

void SkipLevelLifts::liftTarget(
    const uint32_t base_tile,
    const uint32_t dest_level,
    const BitVector& leaf_vector,
    BitVector& out_parent
) const noexcept {
    out_parent.clear();
    if (base_tile >= tiles_.size() || dest_level == 0U
        || dest_level >= tiles_[base_tile].by_level.size()) {
        return;
    }
    applyLiftRows(
        tiles_[base_tile].by_level[dest_level].reverse_transpose,
        leaf_vector,
        out_parent
    );
}

const SkipTileLifts* SkipLevelLifts::tileLifts(const uint32_t base_tile) const noexcept {
    if (base_tile >= tiles_.size()) {
        return nullptr;
    }
    return &tiles_[base_tile];
}

}  // namespace hbrick
