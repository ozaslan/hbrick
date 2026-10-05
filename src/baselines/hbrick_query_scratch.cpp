#include "hbrick/baselines/hbrick_query_scratch.hpp"

#include <algorithm>
#include <cassert>

#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/hbrick_index.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"

namespace hbrick {

namespace {

[[nodiscard]] uint32_t maxGammaBits(const HBrickIndex& index) noexcept {
    uint32_t max_bits = 0U;
    for (const BaseTileSummary& summary : index.brickIndex().tiles().summaries()) {
        max_bits = std::max(max_bits, summary.numPorts());
    }

    for (uint32_t level = 1U; level < index.hierarchy().numLevels(); ++level) {
        for (const SuperTileSummary& summary : index.superLevel(level)) {
            max_bits = std::max(
                max_bits,
                static_cast<uint32_t>(summary.gamma.ports.size())
            );
            max_bits = std::max(
                max_bits,
                static_cast<uint32_t>(summary.exterior_ports.size())
            );
        }
    }

    return max_bits;
}

[[nodiscard]] uint32_t maxExteriorBitsAtLevel(const HBrickIndex& index, const uint32_t level) noexcept {
    if (level == 0U) {
        uint32_t max_bits = 0U;
        for (const BaseTileSummary& summary : index.brickIndex().tiles().summaries()) {
            max_bits = std::max(max_bits, summary.numPorts());
        }
        return max_bits;
    }
    uint32_t max_bits = 0U;
    for (const SuperTileSummary& summary : index.superLevel(level)) {
        max_bits = std::max(
            max_bits,
            static_cast<uint32_t>(summary.exterior_ports.size())
        );
    }
    return max_bits;
}

}  // namespace

void HBrickQueryScratch::prepare(const HBrickIndex& index) {
    const uint32_t max_bits = maxGammaBits(index);
    const uint32_t chain_depth = index.hierarchy().numLevels();

    gamma_a_ = BitVector(max_bits);
    gamma_b_ = BitVector(max_bits);
    gamma_c_ = BitVector(0U);

    source_chain_.resize(chain_depth);
    target_chain_.resize(chain_depth);
    for (uint32_t level = 0U; level < chain_depth; ++level) {
        const uint32_t level_bits = maxExteriorBitsAtLevel(index, level);
        source_chain_[level] = BitVector(level_bits);
        target_chain_[level] = BitVector(level_bits);
    }

    source_ancestor_chain_.clear();
    source_ancestor_chain_.reserve(chain_depth);
    target_ancestor_chain_.clear();
    target_ancestor_chain_.reserve(chain_depth);
}

void HBrickQueryScratch::clearLeafAndWorkspace() noexcept {
    if (!source_chain_.empty()) {
        source_chain_[0U].clear();
    }
    if (!target_chain_.empty()) {
        target_chain_[0U].clear();
    }
    gamma_a_.clear();
    gamma_b_.clear();
    gamma_c_.clear();
}

void HBrickQueryScratch::clearChainLevelsThrough(uint32_t level_end) noexcept {
    assert(level_end <= source_chain_.size());
    assert(level_end <= target_chain_.size());
    for (uint32_t level = 1U; level < level_end; ++level) {
        source_chain_[level].clear();
        target_chain_[level].clear();
    }
}

uint64_t HBrickQueryScratch::memoryBytes() const noexcept {
    uint64_t total = sizeof(HBrickQueryScratch);
    total += static_cast<uint64_t>(source_chain_.capacity()) * sizeof(BitVector);
    total += static_cast<uint64_t>(target_chain_.capacity()) * sizeof(BitVector);
    total += static_cast<uint64_t>(source_ancestor_chain_.capacity()) * sizeof(RegionNodeId);
    total += static_cast<uint64_t>(target_ancestor_chain_.capacity()) * sizeof(RegionNodeId);

    const auto add_vector_words = [&](const BitVector& vector) {
        total += static_cast<uint64_t>(vector.wordsCapacity()) * sizeof(uint64_t);
    };
    add_vector_words(gamma_a_);
    add_vector_words(gamma_b_);
    add_vector_words(gamma_c_);
    for (const BitVector& vector : source_chain_) {
        add_vector_words(vector);
    }
    for (const BitVector& vector : target_chain_) {
        add_vector_words(vector);
    }
    return total;
}

}  // namespace hbrick
