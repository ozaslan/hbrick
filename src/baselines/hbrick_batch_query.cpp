/**
 * @file hbrick_batch_query.cpp
 * @brief Many-source / many-target H-BRICK reachability with shared endpoint work.
 *
 * The batch follows the scalar @ref HBrickSkipLiftBaseline::queryDetailed path
 * exactly, but reorganizes it so that each endpoint frontier is lifted,
 * embedded, and (source side) closed once per ancestor level instead of once
 * per source-target pair. Pairs are then resolved by one bit-vector
 * intersection in the shared ancestor's gamma space.
 */

#include "hbrick/baselines/hbrick_batch_query.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/baselines/hbrick_hierarchy_query.hpp"
#include "hbrick/baselines/hbrick_skip_lift_baseline.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/tile_micro_bfs.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/region_node.hpp"
#include "hbrick/tile/skip_level_lifts.hpp"
#include "hbrick/tile/super_tile_summary.hpp"

namespace hbrick {

namespace {

constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

[[nodiscard]] uint32_t maxBasePortBits(const HBrickIndex& index) noexcept {
    uint32_t max_bits = 0U;
    for (const BaseTileSummary& summary : index.brickIndex().tiles().summaries()) {
        max_bits = std::max(max_bits, summary.numPorts());
    }
    return max_bits;
}

[[nodiscard]] uint32_t maxExteriorBits(const HBrickIndex& index) noexcept {
    uint32_t max_bits = maxBasePortBits(index);
    for (uint32_t level = 1U; level < index.hierarchy().numLevels(); ++level) {
        for (const SuperTileSummary& summary : index.superLevel(level)) {
            max_bits = std::max(
                max_bits,
                static_cast<uint32_t>(summary.exterior_ports.size())
            );
        }
    }
    return max_bits;
}

[[nodiscard]] uint32_t maxGammaBits(const HBrickIndex& index) noexcept {
    uint32_t max_bits = maxExteriorBits(index);
    for (uint32_t level = 1U; level < index.hierarchy().numLevels(); ++level) {
        for (const SuperTileSummary& summary : index.superLevel(level)) {
            max_bits = std::max(
                max_bits,
                static_cast<uint32_t>(summary.gamma.ports.size())
            );
        }
    }
    return max_bits;
}

}  // namespace

void HBrickBatchScratch::prepare(
    const HBrickIndex& index,
    const uint32_t max_sources,
    const uint32_t max_targets
) {
    num_levels_ = index.hierarchy().numLevels();
    base_port_bits_ = maxBasePortBits(index);
    exterior_bits_ = maxExteriorBits(index);
    gamma_bits_ = maxGammaBits(index);
    chain_stride_ = num_levels_;

    sources_.assign(max_sources, Endpoint{});
    targets_.assign(max_targets, Endpoint{});
    source_chains_.assign(static_cast<size_t>(max_sources) * chain_stride_, RegionNodeId{});
    target_chains_.assign(static_cast<size_t>(max_targets) * chain_stride_, RegionNodeId{});

    const auto size_endpoint = [&](Endpoint& endpoint) {
        endpoint.leaf = BitVector(base_port_bits_);
        endpoint.gamma = BitVector(gamma_bits_);
        endpoint.projected = BitVector(gamma_bits_);
    };
    for (Endpoint& endpoint : sources_) {
        size_endpoint(endpoint);
    }
    for (Endpoint& endpoint : targets_) {
        size_endpoint(endpoint);
    }

    lift_workspace_ = BitVector(exterior_bits_);
    pair_first_common_.assign(
        static_cast<size_t>(max_sources) * max_targets,
        0U
    );
    pair_resolved_.assign(
        static_cast<size_t>(max_sources) * max_targets,
        0U
    );
    source_has_level_.assign(max_sources, 0U);
    target_has_level_.assign(max_targets, 0U);
    total_unresolved_ = 0U;

    const uint32_t num_ports = index.brickIndex().ports().numPorts();
    const uint32_t num_vertices =
        index.brickIndex().tiles().decomposition().mapWidth() *
        index.brickIndex().tiles().decomposition().mapHeight();
    port_bfs_scratch_.resetForGraph(num_ports);
    micro_bfs_scratch_.resetForGraph(num_vertices);
}

void HBrickBatchScratch::resetEndpoints(
    const uint32_t source_count,
    const uint32_t target_count
) noexcept {
    for (uint32_t i = 0U; i < source_count; ++i) {
        Endpoint& endpoint = sources_[i];
        endpoint.valid = false;
        endpoint.confined = false;
        endpoint.chain_length = 0U;
        endpoint.embeddable_limit = 0U;
        endpoint.leaf.clear();
        endpoint.gamma.clear();
        endpoint.projected.clear();
    }
    for (uint32_t j = 0U; j < target_count; ++j) {
        Endpoint& endpoint = targets_[j];
        endpoint.valid = false;
        endpoint.confined = false;
        endpoint.chain_length = 0U;
        endpoint.embeddable_limit = 0U;
        endpoint.leaf.clear();
        endpoint.gamma.clear();
        endpoint.projected.clear();
    }
    const size_t pairs = static_cast<size_t>(source_count) * target_count;
    std::fill(pair_first_common_.begin(), pair_first_common_.begin() + pairs, 0U);
    std::fill(pair_resolved_.begin(), pair_resolved_.begin() + pairs, 0U);
    std::fill(source_has_level_.begin(), source_has_level_.begin() + source_count, 0U);
    std::fill(target_has_level_.begin(), target_has_level_.begin() + target_count, 0U);
    total_unresolved_ = static_cast<uint64_t>(source_count) * target_count;
}

uint64_t HBrickBatchScratch::memoryBytes() const noexcept {
    uint64_t total = sizeof(HBrickBatchScratch);
    total += static_cast<uint64_t>(sources_.capacity()) * sizeof(Endpoint);
    total += static_cast<uint64_t>(targets_.capacity()) * sizeof(Endpoint);
    total += static_cast<uint64_t>(source_chains_.capacity()) * sizeof(RegionNodeId);
    total += static_cast<uint64_t>(target_chains_.capacity()) * sizeof(RegionNodeId);
    total += static_cast<uint64_t>(pair_first_common_.capacity()) * sizeof(uint8_t);
    total += static_cast<uint64_t>(pair_resolved_.capacity()) * sizeof(uint8_t);
    total += static_cast<uint64_t>(source_has_level_.capacity()) * sizeof(uint8_t);
    total += static_cast<uint64_t>(target_has_level_.capacity()) * sizeof(uint8_t);
    total += static_cast<uint64_t>(port_bfs_scratch_.memoryBytes());
    total += static_cast<uint64_t>(micro_bfs_scratch_.memoryBytes());
    const auto add_vector_words = [&](const BitVector& vector) {
        total += static_cast<uint64_t>(vector.wordsCapacity()) * sizeof(uint64_t);
    };
    add_vector_words(lift_workspace_);
    for (const Endpoint& endpoint : sources_) {
        add_vector_words(endpoint.leaf);
        add_vector_words(endpoint.gamma);
        add_vector_words(endpoint.projected);
    }
    for (const Endpoint& endpoint : targets_) {
        add_vector_words(endpoint.leaf);
        add_vector_words(endpoint.gamma);
        add_vector_words(endpoint.projected);
    }
    return total;
}

bool HBrickSkipLiftBaseline::prepareBatch(
    const uint32_t max_sources,
    const uint32_t max_targets
) {
    if (status_ != BaselineStatus::Completed || !skip_lifts_.isValid()) {
        return false;
    }
    batch_scratch_.prepare(index_, max_sources, max_targets);
    return true;
}

bool HBrickSkipLiftBaseline::batchQuery(
    const std::span<const uint32_t> sources,
    const std::span<const uint32_t> targets,
    const std::span<uint8_t> out_reachable,
    HBrickBatchQueryStats* stats
) const noexcept {
    return batchQuery(batch_scratch_, sources, targets, out_reachable, stats);
}

bool HBrickSkipLiftBaseline::batchQuery(
    HBrickBatchScratch& scratch,
    const std::span<const uint32_t> sources,
    const std::span<const uint32_t> targets,
    const std::span<uint8_t> out_reachable,
    HBrickBatchQueryStats* stats
) const noexcept {
    const uint32_t num_sources = static_cast<uint32_t>(sources.size());
    const uint32_t num_targets = static_cast<uint32_t>(targets.size());
    const size_t pair_count = static_cast<size_t>(num_sources) * num_targets;

    if (stats != nullptr) {
        *stats = HBrickBatchQueryStats{};
        stats->pairs = static_cast<uint32_t>(pair_count);
    }
    if (out_reachable.size() < pair_count) {
        return false;
    }

    if (status_ != BaselineStatus::Completed || !skip_lifts_.isValid()
        || num_sources == 0U || num_targets == 0U) {
        return false;
    }
    if (scratch.maxSources() < num_sources || scratch.maxTargets() < num_targets
        || scratch.num_levels_ != index_.hierarchy().numLevels()
        || scratch.chain_stride_ < index_.hierarchy().numLevels()
        || scratch.base_port_bits_ < maxBasePortBits(index_)
        || scratch.gamma_bits_ < maxGammaBits(index_)
        || scratch.exterior_bits_ < maxExteriorBits(index_)) {
        return false;
    }

    std::fill(out_reachable.begin(), out_reachable.begin() + pair_count, uint8_t{0});
    scratch.resetEndpoints(num_sources, num_targets);

    const BrickTileIndex& tiles = index_.brickIndex().tiles();
    const HierarchyTree& hierarchy = index_.hierarchy();
    const uint32_t num_vertices =
        tiles.decomposition().mapWidth() * tiles.decomposition().mapHeight();

    // --- Resolve source endpoints -------------------------------------------------
    for (uint32_t i = 0U; i < num_sources; ++i) {
        HBrickBatchScratch::Endpoint& endpoint = scratch.sources_[i];
        const uint32_t source = sources[i];
        if (source >= num_vertices) {
            continue;
        }
        const uint32_t tile = tiles.tileIndexForGlobalVertex(source);
        const uint32_t local = tiles.localIndexForGlobalVertex(source);
        if (tile == kInvalidIndex || local == kInvalidIndex) {
            continue;
        }
        endpoint.valid = true;
        endpoint.tile = tile;
        endpoint.local = local;

        const BaseTileSummary& summary = tiles.summaryByIndex(tile);
        endpoint.leaf.clear();
        endpoint.leaf.rowOrFrom(summary.vertex_to_boundary.row(local));
        if (!endpoint.leaf.any()) {
            endpoint.confined = true;
        }

        const std::span<const RegionNodeId> chain = hierarchy.ancestorChain(tile);
        RegionNodeId* slot =
            scratch.source_chains_.data() + static_cast<size_t>(i) * scratch.chain_stride_;
        std::copy(chain.begin(), chain.end(), slot);
        endpoint.chain_length = static_cast<uint32_t>(chain.size());
    }

    // --- Resolve target endpoints -------------------------------------------------
    for (uint32_t j = 0U; j < num_targets; ++j) {
        HBrickBatchScratch::Endpoint& endpoint = scratch.targets_[j];
        const uint32_t target = targets[j];
        if (target >= num_vertices) {
            continue;
        }
        const uint32_t tile = tiles.tileIndexForGlobalVertex(target);
        const uint32_t local = tiles.localIndexForGlobalVertex(target);
        if (tile == kInvalidIndex || local == kInvalidIndex) {
            continue;
        }
        endpoint.valid = true;
        endpoint.tile = tile;
        endpoint.local = local;

        const BaseTileSummary& summary = tiles.summaryByIndex(tile);
        const size_t target_word_index = local / 64U;
        const uint64_t target_mask = 1ULL << (local % 64U);
        endpoint.leaf.clear();
        const uint32_t num_target_ports = summary.numPorts();
        for (uint32_t port_index = 0U; port_index < num_target_ports; ++port_index) {
            const uint64_t row_word =
                summary.boundary_to_vertex.row(port_index).word(target_word_index);
            if ((row_word & target_mask) != 0U) {
                endpoint.leaf.set(port_index);
            }
        }
        if (!endpoint.leaf.any()) {
            endpoint.confined = true;
        }

        const std::span<const RegionNodeId> chain = hierarchy.ancestorChain(tile);
        RegionNodeId* slot =
            scratch.target_chains_.data() + static_cast<size_t>(j) * scratch.chain_stride_;
        std::copy(chain.begin(), chain.end(), slot);
        endpoint.chain_length = static_cast<uint32_t>(chain.size());
    }

    // --- Embeddable-level limits and per-pair first common ancestor ---------------
    const auto embeddableLimit = [&](const HBrickBatchScratch::Endpoint& endpoint,
                                     const RegionNodeId* chain) noexcept -> uint32_t {
        if (!endpoint.valid) {
            return 0U;
        }
        const SkipTileLifts* tile_lifts = skip_lifts_.tileLifts(endpoint.tile);
        uint32_t limit = 0U;
        for (uint32_t level = 1U; level < endpoint.chain_length; ++level) {
            const uint32_t parent_level = chain[level].level;
            if (parent_level >= hierarchy.numLevels()) {
                break;
            }
            const SuperTileSummary& parent =
                index_.superSummary(parent_level, chain[level].index);
            if (parent.status != BaselineStatus::Completed) {
                break;
            }
            const RegionNode& child = hierarchy.node(chain[level - 1U].level, chain[level - 1U].index);
            if (childEmbeddingIndex(child, parent) == kInvalidIndex) {
                break;
            }
            if (level >= 2U) {
                if (tile_lifts == nullptr || level - 1U >= tile_lifts->by_level.size()
                    || tile_lifts->by_level[level - 1U].forward.numRows() == 0U) {
                    break;
                }
            }
            limit = level;
        }
        return limit;
    };
    for (uint32_t i = 0U; i < num_sources; ++i) {
        HBrickBatchScratch::Endpoint& endpoint = scratch.sources_[i];
        endpoint.embeddable_limit = embeddableLimit(
            endpoint,
            scratch.source_chains_.data() + static_cast<size_t>(i) * scratch.chain_stride_
        );
    }
    for (uint32_t j = 0U; j < num_targets; ++j) {
        HBrickBatchScratch::Endpoint& endpoint = scratch.targets_[j];
        endpoint.embeddable_limit = embeddableLimit(
            endpoint,
            scratch.target_chains_.data() + static_cast<size_t>(j) * scratch.chain_stride_
        );
    }

    for (uint32_t i = 0U; i < num_sources; ++i) {
        const HBrickBatchScratch::Endpoint& source_endpoint = scratch.sources_[i];
        const RegionNodeId* source_chain =
            scratch.source_chains_.data() + static_cast<size_t>(i) * scratch.chain_stride_;
        for (uint32_t j = 0U; j < num_targets; ++j) {
            const HBrickBatchScratch::Endpoint& target_endpoint = scratch.targets_[j];
            const size_t pair = static_cast<size_t>(i) * num_targets + j;

            // The scalar query answers the diagonal before any tile checks.
            if (sources[i] < num_vertices && sources[i] == targets[j]) {
                out_reachable[pair] = 1U;
                scratch.pair_resolved_[pair] = 1U;
                --scratch.total_unresolved_;
                if (stats != nullptr) {
                    ++stats->local_hits;
                }
                continue;
            }
            if (!source_endpoint.valid) {
                // Out-of-range or unresolvable endpoints are unreachable and
                // resolved here so no later stage revisits them.
                scratch.pair_resolved_[pair] = 1U;
                --scratch.total_unresolved_;
                continue;
            }
            if (!target_endpoint.valid) {
                scratch.pair_resolved_[pair] = 1U;
                --scratch.total_unresolved_;
                continue;
            }
            const RegionNodeId* target_chain =
                scratch.target_chains_.data()
                + static_cast<size_t>(j) * scratch.chain_stride_;

            // The scalar query tests the same-tile local closure before it
            // builds the boundary frontiers, so a pair whose endpoints cannot
            // reach a port may still be reachable inside their shared tile.
            if (source_endpoint.tile == target_endpoint.tile) {
                const BaseTileSummary& summary = tiles.summaryByIndex(source_endpoint.tile);
                bool local_hit = false;
                if (!summary.omit_local_closure && summary.local_closure.numRows() > 0U) {
                    local_hit = summary.local_closure.test(
                        source_endpoint.local,
                        target_endpoint.local
                    );
                } else if (graph_ != nullptr) {
                    const TileSlot& slot =
                        tiles.decomposition().slotByIndex(source_endpoint.tile);
                    local_hit = TileMicroBfs::reachable(
                        *graph_,
                        slot.origin,
                        slot.extent.width,
                        slot.extent.height,
                        sources[i],
                        targets[j],
                        scratch.micro_bfs_scratch_
                    ) == ReachabilityAnswer::Reachable;
                }
                if (local_hit) {
                    out_reachable[pair] = 1U;
                    scratch.pair_resolved_[pair] = 1U;
                    --scratch.total_unresolved_;
                    if (stats != nullptr) {
                        ++stats->local_hits;
                    }
                    continue;
                }
            }

            if (source_endpoint.confined || target_endpoint.confined) {
                // No path can leave the source tile or enter the target tile.
                scratch.pair_resolved_[pair] = 1U;
                --scratch.total_unresolved_;
                continue;
            }

            const uint32_t shared_levels = std::min(
                source_endpoint.chain_length,
                target_endpoint.chain_length
            );
            uint8_t first_common = 0U;
            for (uint32_t level = 1U; level < shared_levels; ++level) {
                if (source_chain[level] == target_chain[level]) {
                    first_common = static_cast<uint8_t>(level);
                    break;
                }
            }
            scratch.pair_first_common_[pair] = first_common;
        }
    }

    // --- Level-by-level shared lifts, projections, and pair meets -----------------
    const uint32_t level_count = scratch.numLevels();
    for (uint32_t level = 1U; level < level_count; ++level) {
        if (scratch.total_unresolved_ == 0U) {
            break;
        }
        for (uint32_t i = 0U; i < num_sources; ++i) {
            scratch.source_has_level_[i] = 0U;
            HBrickBatchScratch::Endpoint& endpoint = scratch.sources_[i];
            if (!endpoint.valid || endpoint.confined || endpoint.chain_length <= level
                || endpoint.embeddable_limit < level) {
                continue;
            }
            const RegionNodeId* chain =
                scratch.source_chains_.data() + static_cast<size_t>(i) * scratch.chain_stride_;
            const RegionNodeId parent_id = chain[level];
            const SuperTileSummary& parent =
                index_.superSummary(parent_id.level, parent_id.index);
            const RegionNode& child =
                hierarchy.node(chain[level - 1U].level, chain[level - 1U].index);
            const uint32_t embedding = childEmbeddingIndex(child, parent);
            if (embedding == kInvalidIndex) {
                continue;
            }

            const BitVector* lifted = &endpoint.leaf;
            if (level >= 2U) {
                skip_lifts_.liftSource(endpoint.tile, level - 1U, endpoint.leaf, scratch.lift_workspace_);
                lifted = &scratch.lift_workspace_;
                if (stats != nullptr) {
                    ++stats->source_lifts;
                }
            }
            if (!lifted->any()) {
                // An empty frontier cannot meet any target at this or any
                // higher level, so the embedding and projection are skipped.
                continue;
            }
            embedChildPorts(
                *lifted,
                parent.child_port_to_gamma[embedding],
                endpoint.gamma
            );
            multiplyVectorClosure(
                endpoint.gamma,
                parent.interface_closure,
                endpoint.projected
            );
            scratch.source_has_level_[i] = endpoint.projected.any() ? 1U : 0U;
            if (stats != nullptr) {
                ++stats->source_projections;
                stats->max_level_reached = std::max(stats->max_level_reached, level);
            }
        }

        for (uint32_t j = 0U; j < num_targets; ++j) {
            scratch.target_has_level_[j] = 0U;
            HBrickBatchScratch::Endpoint& endpoint = scratch.targets_[j];
            if (!endpoint.valid || endpoint.confined || endpoint.chain_length <= level
                || endpoint.embeddable_limit < level) {
                continue;
            }
            const RegionNodeId* chain =
                scratch.target_chains_.data() + static_cast<size_t>(j) * scratch.chain_stride_;
            const RegionNodeId parent_id = chain[level];
            const SuperTileSummary& parent =
                index_.superSummary(parent_id.level, parent_id.index);
            const RegionNode& child =
                hierarchy.node(chain[level - 1U].level, chain[level - 1U].index);
            const uint32_t embedding = childEmbeddingIndex(child, parent);
            if (embedding == kInvalidIndex) {
                continue;
            }

            const BitVector* lifted = &endpoint.leaf;
            if (level >= 2U) {
                skip_lifts_.liftTarget(endpoint.tile, level - 1U, endpoint.leaf, scratch.lift_workspace_);
                lifted = &scratch.lift_workspace_;
                if (stats != nullptr) {
                    ++stats->target_lifts;
                }
            }
            if (!lifted->any()) {
                continue;
            }
            embedChildPorts(
                *lifted,
                parent.child_port_to_gamma[embedding],
                endpoint.gamma
            );
            scratch.target_has_level_[j] = endpoint.gamma.any() ? 1U : 0U;
            if (stats != nullptr && scratch.target_has_level_[j] != 0U) {
                stats->max_level_reached = std::max(stats->max_level_reached, level);
            }
        }

        for (uint32_t i = 0U; i < num_sources; ++i) {
            if (scratch.source_has_level_[i] == 0U) {
                continue;
            }
            const HBrickBatchScratch::Endpoint& source_endpoint = scratch.sources_[i];
            for (uint32_t j = 0U; j < num_targets; ++j) {
                if (scratch.target_has_level_[j] == 0U) {
                    continue;
                }
                const size_t pair = static_cast<size_t>(i) * num_targets + j;
                if (scratch.pair_resolved_[pair] != 0U) {
                    continue;
                }
                const uint8_t first_common = scratch.pair_first_common_[pair];
                if (first_common == 0U || level < first_common) {
                    continue;
                }
                const HBrickBatchScratch::Endpoint& target_endpoint = scratch.targets_[j];
                if (stats != nullptr) {
                    ++stats->pair_tests;
                }
                if (source_endpoint.projected.intersects(target_endpoint.gamma)) {
                    out_reachable[pair] = 1U;
                    scratch.pair_resolved_[pair] = 1U;
                    --scratch.total_unresolved_;
                    if (stats != nullptr) {
                        ++stats->ancestor_hits;
                    }
                }
            }
        }
    }

    if (scratch.total_unresolved_ == 0U) {
        return true;
    }

    // --- Remaining pairs: hierarchy negatives or flat BRICK fallback --------------
    const bool hierarchy_sound = index_.hierarchyQuerySound();
    for (uint32_t i = 0U; i < num_sources; ++i) {
        const HBrickBatchScratch::Endpoint& source_endpoint = scratch.sources_[i];
        for (uint32_t j = 0U; j < num_targets; ++j) {
            const size_t pair = static_cast<size_t>(i) * num_targets + j;
            if (scratch.pair_resolved_[pair] != 0U) {
                continue;
            }
            if (!source_endpoint.valid) {
                scratch.pair_resolved_[pair] = 1U;
                continue;
            }
            if (hierarchy_sound) {
                scratch.pair_resolved_[pair] = 1U;
                if (stats != nullptr) {
                    ++stats->hierarchy_negatives;
                }
                continue;
            }

            const ReachabilityAnswer answer = HBrickBaseline::queryFlatBrickPortBfs(
                index_.brickIndex(),
                graph_,
                sources[i],
                targets[j],
                scratch.port_bfs_scratch_,
                scratch.micro_bfs_scratch_
            );
            out_reachable[pair] = answer == ReachabilityAnswer::Reachable ? 1U : 0U;
            scratch.pair_resolved_[pair] = 1U;
            if (stats != nullptr) {
                ++stats->fallbacks;
            }
        }
    }
    return true;
}

}  // namespace hbrick
