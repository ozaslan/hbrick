#include "hbrick/baselines/hbrick_query_scratch.hpp"

#include <algorithm>

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

}  // namespace

void HBrickQueryScratch::prepare(const HBrickIndex& index) {
    const uint32_t max_bits = maxGammaBits(index);
    const uint32_t chain_depth = index.hierarchy().numLevels();

    gamma_a_ = BitVector(max_bits);
    gamma_b_ = BitVector(max_bits);
    gamma_c_ = BitVector(max_bits);

    source_chain_.resize(chain_depth);
    target_chain_.resize(chain_depth);
    for (uint32_t level = 0U; level < chain_depth; ++level) {
        source_chain_[level] = BitVector(max_bits);
        target_chain_[level] = BitVector(max_bits);
    }

    source_ancestor_chain_.clear();
    source_ancestor_chain_.reserve(chain_depth);
    target_ancestor_chain_.clear();
    target_ancestor_chain_.reserve(chain_depth);
}

void HBrickQueryScratch::clearVectors() noexcept {
    for (BitVector& vector : source_chain_) {
        vector.clear();
    }
    for (BitVector& vector : target_chain_) {
        vector.clear();
    }
    gamma_a_.clear();
    gamma_b_.clear();
    gamma_c_.clear();
}

}  // namespace hbrick
