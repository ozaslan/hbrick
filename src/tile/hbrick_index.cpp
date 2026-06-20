#include "hbrick/tile/hbrick_index.hpp"

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/super_tile_composer.hpp"

namespace hbrick {

namespace {

[[nodiscard]] TileSlot tileSlotFromRegion(const RegionNode& region) noexcept {
    TileSlot slot{};
    slot.tile_i = region.region_i;
    slot.tile_j = region.region_j;
    slot.origin = region.origin;
    slot.extent = region.extent;
    return slot;
}

}  // namespace

HBrickIndex HBrickIndex::build(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    HBrickConfig config
) {
    HBrickIndex index;
    index.status_ = BaselineStatus::NotRun;
    index.config_ = config;

    if (!config.base_tile_size.isValid() || !config.group_size.isValid()) {
        index.status_ = BaselineStatus::Failed;
        return index;
    }
    if (config.max_depth == 0U) {
        index.status_ = BaselineStatus::Failed;
        return index;
    }

    index.brick_index_ = BrickIndex::build(
        graph,
        layout,
        config.base_tile_size,
        config.max_memory_bytes
    );
    if (index.brick_index_.status() != BaselineStatus::Completed) {
        index.status_ = index.brick_index_.status();
        return index;
    }

    index.hierarchy_ = HierarchyTree::build(
        index.brick_index_.tiles().decomposition(),
        config.group_size,
        config.max_depth
    );

    if (index.hierarchy_.numLevels() <= 1U) {
        index.status_ = BaselineStatus::Completed;
        return index;
    }

    index.super_summaries_.resize(static_cast<std::size_t>(index.hierarchy_.numLevels() - 1U));

    for (uint32_t level = 1U; level < index.hierarchy_.numLevels(); ++level) {
        const std::span<const RegionNode> regions = index.hierarchy_.level(level);
        std::vector<SuperTileSummary>& level_summaries =
            index.super_summaries_[static_cast<std::size_t>(level - 1U)];
        level_summaries.resize(regions.size());

        for (uint32_t node_index = 0U; node_index < regions.size(); ++node_index) {
            const RegionNode& region = regions[node_index];
            const TileSlot parent_slot = tileSlotFromRegion(region);

            std::vector<ChildBoundarySummary> children;
            std::vector<std::vector<GridCoord>> child_port_storage;
            child_port_storage.reserve(region.children.size());
            children.reserve(region.children.size());

            for (const RegionNodeId& child_id : region.children) {
                child_port_storage.emplace_back();
                if (child_id.level == 0U) {
                    const BaseTileSummary& base_summary =
                        index.brick_index_.tiles().summaryByIndex(child_id.index);
                    children.push_back(childBoundaryFromBaseTile(
                        base_summary,
                        child_id.index,
                        child_port_storage.back()
                    ));
                } else {
                    const SuperTileSummary& child_summary =
                        index.super_summaries_[static_cast<std::size_t>(child_id.level - 1U)]
                                              [child_id.index];
                    children.push_back(childBoundaryFromSuperTile(
                        child_summary,
                        child_port_storage.back()
                    ));
                }
            }

            SuperTileSummary composed = composeSuperTile(
                parent_slot,
                children,
                index.brick_index_.ports(),
                index.brick_index_.seamEdges(),
                config.max_memory_bytes
            );
            if (composed.status != BaselineStatus::Completed) {
                index.status_ = composed.status;
                return index;
            }

            level_summaries[node_index] = std::move(composed);
        }
    }

    index.status_ = BaselineStatus::Completed;
    return index;
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
