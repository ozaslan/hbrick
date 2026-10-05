#include "hbrick/tile/hbrick_index.hpp"

#include <algorithm>

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_index_builder.hpp"
#include "hbrick/tile/seam_edge.hpp"

namespace hbrick {

namespace {

template<typename T>
[[nodiscard]] uint64_t vectorHeapBytes(const std::vector<T>& values) noexcept {
    return static_cast<uint64_t>(values.capacity()) * static_cast<uint64_t>(sizeof(T));
}

uint64_t estimateBrickStorageBytes(const BrickIndex& brick_index) noexcept {
    if (brick_index.status() != BaselineStatus::Completed) {
        return 0U;
    }
    return brick_index.measureStorageBytes();
}

uint64_t estimateHBrickExtraStorageBytes(const HBrickIndex& index) noexcept {
    if (index.status() != BaselineStatus::Completed) {
        return 0U;
    }

    const uint64_t measured = index.measureStorageBytes();
    const uint64_t brick = estimateBrickStorageBytes(index.brickIndex());
    return measured > brick ? measured - brick : 0U;
}

}  // namespace

HBrickIndex HBrickIndex::build(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    HBrickConfig config
) {
    HBrickIndexBuilder builder;
    builder.begin(graph, layout, config);
    while (!builder.step()) {
    }
    return builder.takeIndex();
}

HBrickStorageEstimate HBrickIndex::estimateStorageBreakdown() const noexcept {
    HBrickStorageEstimate estimate{};
    if (status_ != BaselineStatus::Completed) {
        return estimate;
    }

    estimate.brick_bytes = estimateBrickStorageBytes(brick_index_);
    estimate.hbrick_extra_bytes = estimateHBrickExtraStorageBytes(*this);
    return estimate;
}

uint64_t HBrickIndex::estimateStorageBytes() const noexcept {
    return estimateStorageBreakdown().totalBytes();
}

uint64_t HBrickIndex::measureStorageBytes() const noexcept {
    uint64_t bytes = brick_index_.measureStorageBytes();
    bytes += hierarchy_.measureStorageBytes();
    bytes += vectorHeapBytes(super_summaries_);
    for (uint32_t level = 1U; level < hierarchy_.numLevels(); ++level) {
        bytes += vectorHeapBytes(super_summaries_[static_cast<std::size_t>(level - 1U)]);
        for (const SuperTileSummary& summary : superLevel(level)) {
            bytes += summary.interface_closure.memoryBytes();
            bytes += summary.interface_closure_transpose.memoryBytes();
            bytes += summary.boundary_summary.memoryBytes();
            bytes += vectorHeapBytes(summary.child_port_to_gamma);
            for (const std::vector<uint32_t>& port_to_gamma : summary.child_port_to_gamma) {
                bytes += vectorHeapBytes(port_to_gamma);
            }
            bytes += vectorHeapBytes(summary.child_embedding_of);
            bytes += vectorHeapBytes(summary.exterior_ports);
            bytes += vectorHeapBytes(summary.exterior_gamma_indices);
            bytes += vectorHeapBytes(summary.gamma.ports);
        }
    }
    bytes += build_report_.measureStorageBytes();
    return bytes;
}

std::vector<HBrickLevelGraphStats> HBrickIndex::levelGraphStats() const noexcept {
    std::vector<HBrickLevelGraphStats> stats;
    if (status_ != BaselineStatus::Completed) {
        return stats;
    }

    HBrickLevelGraphStats l0{};
    l0.level = 0U;
    l0.num_graphs = 1U;
    const uint32_t port_nodes = brick_index_.ports().numPorts();
    l0.total_nodes = port_nodes;
    l0.max_nodes = port_nodes;
    stats.push_back(l0);

    for (uint32_t level = 1U; level < hierarchy_.numLevels(); ++level) {
        HBrickLevelGraphStats level_stats{};
        level_stats.level = level;
        for (const SuperTileSummary& summary : superLevel(level)) {
            if (summary.status != BaselineStatus::Completed) {
                continue;
            }
            const uint32_t nodes = static_cast<uint32_t>(summary.gamma.ports.size());
            if (nodes == 0U) {
                continue;
            }
            ++level_stats.num_graphs;
            level_stats.total_nodes += nodes;
            level_stats.max_nodes = std::max(level_stats.max_nodes, nodes);
        }
        stats.push_back(level_stats);
    }

    return stats;
}

bool HBrickIndex::hasSuperLevel(const uint32_t level) const noexcept {
    return level >= 1U && level < hierarchy_.numLevels();
}

bool HBrickIndex::hierarchyQuerySound() const noexcept {
    return hierarchy_query_sound_;
}

void HBrickIndex::updateHierarchyQuerySound() noexcept {
    if (status_ != BaselineStatus::Completed || !hasSuperLevel(1U)) {
        hierarchy_query_sound_ = false;
        return;
    }

    // Cross-region paths are complete only when every leaf shares one root.
    const uint32_t top_level = hierarchy_.numLevels() - 1U;
    if (hierarchy_.level(top_level).size() != 1U) {
        hierarchy_query_sound_ = false;
        return;
    }

    for (uint32_t level = 1U; level < hierarchy_.numLevels(); ++level) {
        for (const SuperTileSummary& summary : superLevel(level)) {
            if (summary.status == BaselineStatus::Completed) {
                continue;
            }
            // A super-tile skipped because none of its children exposes a
            // boundary port (a region lying entirely inside obstacles) has an
            // empty Gamma_U. No path can enter or leave such a region, so its
            // absence cannot hide a cross-region path; the hierarchy remains
            // complete for negatives.
            if (summary.status == BaselineStatus::SkippedByPolicy
                && summary.gamma.ports.empty()
                && summary.exterior_ports.empty()) {
                continue;
            }
            hierarchy_query_sound_ = false;
            return;
        }
    }
    hierarchy_query_sound_ = true;
}

const SuperTileSummary& HBrickIndex::superSummary(
    const uint32_t level,
    const uint32_t node_index
) const noexcept {
    return super_summaries_[static_cast<std::size_t>(level - 1U)][node_index];
}

std::span<const SuperTileSummary> HBrickIndex::superLevel(
    const uint32_t level
) const noexcept {
    if (!hasSuperLevel(level)) {
        return {};
    }
    return super_summaries_[static_cast<std::size_t>(level - 1U)];
}

}  // namespace hbrick
