#include "hbrick/tile/hbrick_index.hpp"

#include <algorithm>

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_index_builder.hpp"
#include "hbrick/tile/seam_edge.hpp"

namespace hbrick {

namespace {

uint64_t estimateBrickStorageBytes(const BrickIndex& brick_index) noexcept {
    if (brick_index.status() != BaselineStatus::Completed) {
        return 0U;
    }

    uint64_t bytes = 0U;
    for (const BaseTileSummary& summary : brick_index.tiles().summaries()) {
        bytes += summary.local_closure.memoryBytes();
        bytes += summary.boundary_summary.memoryBytes();
        bytes += summary.vertex_to_boundary.memoryBytes();
        bytes += summary.boundary_to_vertex.memoryBytes();
    }

    bytes += brick_index.ports().estimateStorageBytes();
    bytes += brick_index.portGraph().estimateStorageBytes();
    bytes += static_cast<uint64_t>(brick_index.seamEdges().size()) * sizeof(SeamEdge);
    return bytes;
}

uint64_t estimateHBrickExtraStorageBytes(const HBrickIndex& index) noexcept {
    if (index.status() != BaselineStatus::Completed) {
        return 0U;
    }

    uint64_t bytes = 0U;
    for (uint32_t level = 1U; level < index.hierarchy().numLevels(); ++level) {
        for (const SuperTileSummary& summary : index.superLevel(level)) {
            bytes += summary.interface_closure.memoryBytes();
            bytes += summary.boundary_summary.memoryBytes();
            for (const BitMatrix& embedding : summary.child_embeddings) {
                bytes += embedding.memoryBytes();
            }
        }
    }
    return bytes;
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
