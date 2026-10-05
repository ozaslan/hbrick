/**
 * @file fused_tree_lifts.cpp
 * @brief Implementation of precomputed rectangular tree-edge lift operators.
 */

#include "hbrick/tile/fused_tree_lifts.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

#include "hbrick/tile/hbrick_index.hpp"

namespace hbrick {

FusedTreeLifts FusedTreeLifts::build(const HBrickIndex& index) {
    FusedTreeLifts result;
    if (index.status() != BaselineStatus::Completed) {
        return result;
    }

    const HierarchyTree& hierarchy = index.hierarchy();
    const uint32_t num_levels = hierarchy.numLevels();
    if (num_levels <= 1U) {
        result.is_valid_ = true;
        return result;
    }

    result.levels_.resize(num_levels);

    for (uint32_t level = 1U; level < num_levels; ++level) {
        if (!index.hasSuperLevel(level)) {
            continue;
        }

        const uint32_t num_regions = static_cast<uint32_t>(hierarchy.level(level).size());
        result.levels_[level].resize(num_regions);

        for (uint32_t region = 0U; region < num_regions; ++region) {
            const SuperTileSummary& summary = index.superSummary(level, region);
            if (summary.status != BaselineStatus::Completed) {
                continue;
            }

            const uint32_t num_embeddings = static_cast<uint32_t>(summary.child_port_to_gamma.size());
            const uint32_t num_ext = static_cast<uint32_t>(summary.exterior_gamma_indices.size());
            const uint32_t gamma_size = summary.interface_closure.numRows();

            std::vector<uint32_t> gamma_to_ext(gamma_size, UINT32_MAX);
            for (uint32_t e = 0U; e < num_ext; ++e) {
                const uint32_t gamma_e = summary.exterior_gamma_indices[e];
                if (gamma_e < gamma_size) {
                    gamma_to_ext[gamma_e] = e;
                }
            }

            auto projectClosedRow = [&](const BitVector& closed_row, BitMatrix& out, const uint32_t child_port) noexcept {
                const uint32_t gamma_limit = std::min(
                    static_cast<uint32_t>(closed_row.numBits()),
                    gamma_size
                );
                const uint64_t* words = closed_row.wordsData();
                const size_t num_words = closed_row.numWords();
                for (size_t w = 0U; w < num_words; ++w) {
                    uint64_t word = words[w];
                    if (word == 0U) {
                        continue;
                    }
                    const size_t bit_base = w * 64U;
                    if (bit_base >= static_cast<size_t>(gamma_limit)) {
                        break;
                    }
                    do {
                        const unsigned bit = static_cast<unsigned>(std::countr_zero(word));
                        const uint32_t gamma = static_cast<uint32_t>(bit_base + bit);
                        word &= word - 1U;
                        if (gamma >= gamma_limit) {
                            continue;
                        }
                        const uint32_t ext_index = gamma_to_ext[gamma];
                        if (ext_index != UINT32_MAX) {
                            out.set(child_port, ext_index);
                        }
                    } while (word != 0U);
                }
            };

            FusedParentLift parent_lift;
            parent_lift.children.resize(num_embeddings);

            for (uint32_t k = 0U; k < num_embeddings; ++k) {
                const auto& port_to_gamma = summary.child_port_to_gamma[k];
                const uint32_t num_child_ports = static_cast<uint32_t>(port_to_gamma.size());

                BitMatrix fwd(num_child_ports, num_ext);
                BitMatrix rev_t(num_child_ports, num_ext);

                for (uint32_t p = 0U; p < num_child_ports; ++p) {
                    const uint32_t gamma_p = port_to_gamma[p];
                    if (gamma_p == UINT32_MAX || gamma_p >= gamma_size) {
                        continue;
                    }
                    projectClosedRow(summary.interface_closure.row(gamma_p), fwd, p);
                    projectClosedRow(summary.interface_closure_transpose.row(gamma_p), rev_t, p);
                }

                parent_lift.children[k].forward = std::move(fwd);
                parent_lift.children[k].reverse_transpose = std::move(rev_t);
            }

            result.levels_[level][region] = std::move(parent_lift);
        }
    }

    result.is_valid_ = true;
    return result;
}

uint64_t FusedTreeLifts::memoryBytes() const noexcept {
    uint64_t total = sizeof(FusedTreeLifts);
    total += static_cast<uint64_t>(levels_.capacity()) * sizeof(std::vector<FusedParentLift>);
    for (const auto& level_vec : levels_) {
        total += static_cast<uint64_t>(level_vec.capacity()) * sizeof(FusedParentLift);
        for (const auto& parent : level_vec) {
            total += static_cast<uint64_t>(parent.children.capacity()) * sizeof(FusedChildLift);
            for (const auto& child : parent.children) {
                total += child.forward.memoryBytes();
                total += child.reverse_transpose.memoryBytes();
            }
        }
    }
    return total;
}

void FusedTreeLifts::liftSource(
    const uint32_t level,
    const uint32_t parent_region,
    const uint32_t child_embedding_index,
    const BitVector& child_vector,
    BitVector& out_parent
) const noexcept {
    out_parent.clear();
    if (level >= levels_.size() || parent_region >= levels_[level].size()) {
        return;
    }
    const auto& parent_lift = levels_[level][parent_region];
    if (child_embedding_index >= parent_lift.children.size()) {
        return;
    }
    const BitMatrix& lift = parent_lift.children[child_embedding_index].forward;
    const uint32_t row_limit = lift.numRows();
    const uint64_t* words = child_vector.wordsData();
    const size_t num_words = child_vector.numWords();

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

void FusedTreeLifts::liftTarget(
    const uint32_t level,
    const uint32_t parent_region,
    const uint32_t child_embedding_index,
    const BitVector& child_vector,
    BitVector& out_parent
) const noexcept {
    out_parent.clear();
    if (level >= levels_.size() || parent_region >= levels_[level].size()) {
        return;
    }
    const auto& parent_lift = levels_[level][parent_region];
    if (child_embedding_index >= parent_lift.children.size()) {
        return;
    }
    const BitMatrix& lift = parent_lift.children[child_embedding_index].reverse_transpose;
    const uint32_t row_limit = lift.numRows();
    const uint64_t* words = child_vector.wordsData();
    const size_t num_words = child_vector.numWords();

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

const FusedChildLift* FusedTreeLifts::childLift(
    const uint32_t level,
    const uint32_t parent_region,
    const uint32_t child_embedding_index
) const noexcept {
    if (level >= levels_.size() || parent_region >= levels_[level].size()) {
        return nullptr;
    }
    const auto& parent_lift = levels_[level][parent_region];
    if (child_embedding_index >= parent_lift.children.size()) {
        return nullptr;
    }
    return &parent_lift.children[child_embedding_index];
}

}  // namespace hbrick
