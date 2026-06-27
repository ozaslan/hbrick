#include "hbrick/tile/hbrick_index_builder.hpp"

#include <chrono>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>

#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/port_graph.hpp"
#include "hbrick/tile/port_index.hpp"
#include "hbrick/tile/super_tile_composer.hpp"
#include "hbrick/tile/tile_closure_util.hpp"
#include "hbrick/tile/tile_decomposition.hpp"

namespace hbrick {

namespace {

enum class FinalizeSubstep : uint8_t {
    PortIndex = 0,
    SeamEdges = 1,
    PortGraph = 2,
};

constexpr uint32_t kSeamVerticesPerStep = 131072U;
constexpr uint32_t kPortGraphTilesPerStep = 64U;

[[nodiscard]] uint64_t monotonicNowNanoseconds() noexcept {
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock::now().time_since_epoch()
    ).count());
}

[[nodiscard]] TileSlot tileSlotFromRegion(const RegionNode& region) noexcept {
    TileSlot slot{};
    slot.tile_i = region.region_i;
    slot.tile_j = region.region_j;
    slot.origin = region.origin;
    slot.extent = region.extent;
    return slot;
}

[[nodiscard]] uint32_t countSuperRegions(const HierarchyTree& hierarchy) noexcept {
    uint32_t count = 0U;
    for (uint32_t level = 1U; level < hierarchy.numLevels(); ++level) {
        count += static_cast<uint32_t>(hierarchy.level(level).size());
    }
    return count;
}

void countBaseTileOutcomes(
    const BrickTileIndex& tile_index,
    uint32_t& completed,
    uint32_t& skipped,
    uint32_t& with_closure,
    uint32_t& empty_impassable
) noexcept {
    completed = 0U;
    skipped = 0U;
    with_closure = 0U;
    empty_impassable = 0U;
    for (const BaseTileSummary& summary : tile_index.summaries()) {
        if (summary.status == BaselineStatus::Completed) {
            ++completed;
            if (summary.numLocalVertices() > 0U) {
                ++with_closure;
            } else {
                ++empty_impassable;
            }
        } else if (summary.status == BaselineStatus::SkippedByPolicy) {
            ++skipped;
        }
    }
}

[[nodiscard]] const char* baseTileFailureDetail(const BaselineStatus status) noexcept {
    switch (status) {
        case BaselineStatus::SkippedByPolicy:
            return "one or more base tile closures exceeded the memory budget";
        case BaselineStatus::Failed:
            return "one or more base tiles failed to build";
        default:
            return "base tile preprocess failed";
    }
}

[[nodiscard]] const char* superTileFailureDetail(
    const BaselineStatus status,
    const uint32_t level,
    const uint32_t node_index
) noexcept {
    (void)level;
    (void)node_index;
    switch (status) {
        case BaselineStatus::SkippedByPolicy:
            return "super-tile skipped (no composable child boundaries or memory budget)";
        case BaselineStatus::Failed:
            return "super-tile composition failed (invalid child boundaries or empty port set)";
        default:
            return "super-tile composition failed";
    }
}

}  // namespace

void HBrickIndexBuilder::begin(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    HBrickConfig config
) {
    graph_ = &graph;
    layout_ = &layout;
    if (isUnlimitedMemoryBudget(config.max_memory_bytes)) {
        config.max_memory_bytes = kHBrickUnlimitedMemoryBytes;
    }
    config_ = config;
    index_ = HBrickIndex{};
    index_.config_ = config;
    index_.status_ = BaselineStatus::NotRun;
    tile_index_ = BrickTileIndex{};
    decomposition_ = TileDecomposition{};
    progress_ = HBrickBuildProgress{};
    report_ = HBrickBuildReport{};
    phase_ = HBrickBuildStage::Idle;
    next_base_tile_index_ = 0U;
    super_level_ = 1U;
    super_node_index_ = 0U;
    total_super_regions_ = 0U;
    finalize_substep_ = static_cast<uint8_t>(FinalizeSubstep::PortIndex);
    seam_vertex_cursor_ = 0U;
    port_graph_tile_cursor_ = 0U;
    port_graph_builder_.reset();
    cancelled_ = false;
    build_started_ns_ = monotonicNowNanoseconds();
    phase_started_ns_ = build_started_ns_;
    base_tile_started_ns_ = 0U;
    super_compose_started_ns_ = 0U;

    if (!config.base_tile_size.isValid() || !config.group_size.isValid()) {
        finishFailure(BaselineStatus::Failed, "invalid tile or group size");
        return;
    }
    if (config.max_depth == 0U) {
        finishFailure(BaselineStatus::Failed, "max_depth must be at least 1");
        return;
    }

    decomposition_ = TileDecomposition::decompose(
        graph.width(),
        graph.height(),
        config.base_tile_size
    );
    tile_index_.decomposition_ = decomposition_;
    tile_index_.summaries_.resize(decomposition_.numSlots());
    tile_index_.global_vertex_tile_index_.assign(
        graph.numVertices(),
        std::numeric_limits<uint32_t>::max()
    );
    tile_index_.global_vertex_local_index_.assign(
        graph.numVertices(),
        std::numeric_limits<uint32_t>::max()
    );

    report_.num_base_tiles = decomposition_.numSlots();
    progress_.num_base_tiles = decomposition_.numSlots();
    progress_.stage = HBrickBuildStage::BaseTiles;
    progress_.work_total = static_cast<uint64_t>(decomposition_.numSlots()) + 2U;
    phase_ = HBrickBuildStage::BaseTiles;
    base_tile_started_ns_ = monotonicNowNanoseconds();
}

bool HBrickIndexBuilder::step() noexcept {
    if (phase_ == HBrickBuildStage::Idle || phase_ == HBrickBuildStage::Finished) {
        return true;
    }
    if (cancelled_) {
        finishFailure(BaselineStatus::Failed, "build cancelled");
        return true;
    }

    switch (phase_) {
        case HBrickBuildStage::BaseTiles: {
            if (next_base_tile_index_ >= decomposition_.numSlots()) {
                report_.base_tile_nanoseconds =
                    monotonicNowNanoseconds() - base_tile_started_ns_;
                countBaseTileOutcomes(
                    tile_index_,
                    report_.num_base_completed,
                    report_.num_base_skipped,
                    report_.num_base_with_closure,
                    report_.num_base_empty_impassable
                );
                if (!tile_index_.allTilesCompleted()) {
                    finishFailure(
                        BaselineStatus::SkippedByPolicy,
                        baseTileFailureDetail(BaselineStatus::SkippedByPolicy)
                    );
                    return true;
                }
                phase_ = HBrickBuildStage::FinalizeBrick;
                phase_started_ns_ = monotonicNowNanoseconds();
                return false;
            }

            const TileSlot& slot = decomposition_.slotByIndex(next_base_tile_index_);
            uint64_t tile_closure_ns = 0U;
            BaseTileSummary summary = buildBaseTile(
                *graph_,
                *layout_,
                slot,
                config_.max_memory_bytes,
                &tile_closure_ns,
                &base_tile_closure_scratch_
            );
            report_.base_tile_closure_nanoseconds += tile_closure_ns;
            tile_index_.summaries_[next_base_tile_index_] = std::move(summary);

            const BaseTileSummary& stored =
                tile_index_.summaries_[next_base_tile_index_];
            if (stored.status == BaselineStatus::Completed) {
                for (uint32_t local_index = 0U; local_index < stored.numLocalVertices();
                     ++local_index) {
                    const uint32_t global_vertex = stored.global_vertices[local_index];
                    tile_index_.global_vertex_tile_index_[global_vertex] =
                        next_base_tile_index_;
                    tile_index_.global_vertex_local_index_[global_vertex] = local_index;
                }
            }

            progress_.current_base_tile_index = next_base_tile_index_;
            ++next_base_tile_index_;
            advanceWork();
            return false;
        }
        case HBrickBuildStage::FinalizeBrick: {
            BrickIndex& brick_index = index_.brick_index_;
            switch (static_cast<FinalizeSubstep>(finalize_substep_)) {
                case FinalizeSubstep::PortIndex: {
                    brick_index.tile_index_ = std::move(tile_index_);
                    tile_index_ = BrickTileIndex{};
                    if (!brick_index.tile_index_.allTilesCompleted()) {
                        finishFailure(
                            BaselineStatus::SkippedByPolicy,
                            "flat BRICK assembly skipped incomplete base tiles"
                        );
                        return true;
                    }

                    brick_index.port_index_ = PortIndex::build(
                        brick_index.tile_index_,
                        graph_->numVertices()
                    );
                    brick_index.seam_edges_.clear();
                    seam_vertex_cursor_ = 0U;
                    finalize_substep_ = static_cast<uint8_t>(FinalizeSubstep::SeamEdges);
                    return false;
                }
                case FinalizeSubstep::SeamEdges: {
                    const uint32_t vertex_end = std::min(
                        seam_vertex_cursor_ + kSeamVerticesPerStep,
                        graph_->numVertices()
                    );
                    collectSeamEdgesForVertexRange(
                        graph_->csrGraph(),
                        brick_index.tile_index_,
                        brick_index.port_index_,
                        seam_vertex_cursor_,
                        vertex_end,
                        brick_index.seam_edges_
                    );
                    seam_vertex_cursor_ = vertex_end;
                    if (seam_vertex_cursor_ < graph_->numVertices()) {
                        return false;
                    }

                    port_graph_tile_cursor_ = 0U;
                    port_graph_builder_.emplace(brick_index.port_index_.numPorts());
                    finalize_substep_ = static_cast<uint8_t>(FinalizeSubstep::PortGraph);
                    return false;
                }
                case FinalizeSubstep::PortGraph: {
                    if (!port_graph_builder_.has_value()) {
                        finishFailure(BaselineStatus::Failed, "port graph builder missing");
                        return true;
                    }

                    const uint32_t num_tiles = brick_index.tile_index_.decomposition().numSlots();
                    const uint32_t tile_end = std::min(
                        port_graph_tile_cursor_ + kPortGraphTilesPerStep,
                        num_tiles
                    );
                    for (uint32_t tile_index_value = port_graph_tile_cursor_;
                         tile_index_value < tile_end;
                         ++tile_index_value) {
                        addIntraTilePortEdgesForTile(
                            tile_index_value,
                            brick_index.tile_index_,
                            brick_index.port_index_,
                            *port_graph_builder_
                        );
                    }
                    port_graph_tile_cursor_ = tile_end;
                    if (port_graph_tile_cursor_ < num_tiles) {
                        return false;
                    }

                    addSeamEdgesToPortGraph(*port_graph_builder_, brick_index.seam_edges_);
                    brick_index.port_graph_ = port_graph_builder_->build();
                    port_graph_builder_.reset();
                    brick_index.status_ = BaselineStatus::Completed;

                    report_.brick_finalize_nanoseconds =
                        monotonicNowNanoseconds() - phase_started_ns_;
                    report_.num_ports = brick_index.port_index_.numPorts();
                    report_.num_seams =
                        static_cast<uint32_t>(brick_index.seam_edges_.size());

                    finalize_substep_ = static_cast<uint8_t>(FinalizeSubstep::PortIndex);
                    phase_ = HBrickBuildStage::Hierarchy;
                    phase_started_ns_ = monotonicNowNanoseconds();
                    advanceWork();
                    return false;
                }
            }

            finishFailure(BaselineStatus::Failed, "flat BRICK assembly failed");
            return true;
        }
        case HBrickBuildStage::Hierarchy: {
            report_.hierarchy_nanoseconds =
                monotonicNowNanoseconds() - phase_started_ns_;
            index_.hierarchy_ = HierarchyTree::build(
                index_.brick_index_.tiles().decomposition(),
                config_.group_size,
                config_.max_depth
            );
            report_.num_hierarchy_levels = index_.hierarchy_.numLevels();
            total_super_regions_ = countSuperRegions(index_.hierarchy_);
            progress_.total_super_regions = total_super_regions_;
            progress_.work_total = static_cast<uint64_t>(decomposition_.numSlots())
                + 2U + static_cast<uint64_t>(total_super_regions_);
            advanceWork();

            if (index_.hierarchy_.numLevels() <= 1U) {
                finishSuccess();
                return true;
            }

            index_.super_summaries_.resize(
                static_cast<std::size_t>(index_.hierarchy_.numLevels() - 1U)
            );
            report_.super_levels.resize(
                static_cast<std::size_t>(index_.hierarchy_.numLevels() - 1U)
            );
            for (uint32_t level = 1U; level < index_.hierarchy_.numLevels(); ++level) {
                HBrickSuperLevelReport& level_report =
                    report_.super_levels[static_cast<std::size_t>(level - 1U)];
                level_report.level = level;
                level_report.num_regions =
                    static_cast<uint32_t>(index_.hierarchy_.level(level).size());
                index_.super_summaries_[static_cast<std::size_t>(level - 1U)].resize(
                    level_report.num_regions
                );
            }

            super_level_ = 1U;
            super_node_index_ = 0U;
            super_compose_started_ns_ = monotonicNowNanoseconds();
            phase_ = HBrickBuildStage::SuperTiles;
            progress_.stage = HBrickBuildStage::SuperTiles;
            return false;
        }
        case HBrickBuildStage::SuperTiles: {
            if (super_level_ >= index_.hierarchy_.numLevels()) {
                report_.super_compose_nanoseconds =
                    monotonicNowNanoseconds() - super_compose_started_ns_;
                finishSuccess();
                return true;
            }

            const std::span<const RegionNode> regions =
                index_.hierarchy_.level(super_level_);
            if (super_node_index_ >= regions.size()) {
                ++super_level_;
                super_node_index_ = 0U;
                return false;
            }

            const RegionNode& region = regions[super_node_index_];
            const TileSlot parent_slot = tileSlotFromRegion(region);

            std::vector<ChildBoundarySummary> children;
            std::vector<std::vector<GridCoord>> child_port_storage;
            child_port_storage.reserve(region.children.size());
            children.reserve(region.children.size());

            for (const RegionNodeId& child_id : region.children) {
                child_port_storage.emplace_back();
                if (child_id.level == 0U) {
                    const BaseTileSummary& base_summary =
                        index_.brick_index_.tiles().summaryByIndex(child_id.index);
                    children.push_back(childBoundaryFromBaseTile(
                        base_summary,
                        child_id.index,
                        child_port_storage.back()
                    ));
                } else {
                    const SuperTileSummary& child_summary =
                        index_.super_summaries_[static_cast<std::size_t>(child_id.level - 1U)]
                                              [child_id.index];
                    children.push_back(childBoundaryFromSuperTile(
                        child_summary,
                        child_port_storage.back()
                    ));
                }
            }

            const uint64_t node_started_ns = monotonicNowNanoseconds();
            SuperTileSummary composed = composeSuperTile(
                parent_slot,
                children,
                index_.brick_index_.ports(),
                index_.brick_index_.seamEdges(),
                config_.max_memory_bytes,
                &base_tile_closure_scratch_
            );
            const uint64_t node_elapsed_ns = monotonicNowNanoseconds() - node_started_ns;

            HBrickSuperLevelReport& level_report =
                report_.super_levels[static_cast<std::size_t>(super_level_ - 1U)];
            level_report.compose_nanoseconds += node_elapsed_ns;

            if (composed.status != BaselineStatus::Completed) {
                if (composed.status == BaselineStatus::SkippedByPolicy) {
                    ++level_report.num_skipped;
                    index_.super_summaries_[static_cast<std::size_t>(super_level_ - 1U)]
                                           [super_node_index_] = std::move(composed);
                    progress_.current_level = super_level_;
                    progress_.current_node_index = super_node_index_;
                    ++super_node_index_;
                    advanceWork();
                    return false;
                }

                char detail[128];
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "L%u region %u: %s",
                    super_level_,
                    super_node_index_,
                    superTileFailureDetail(composed.status, super_level_, super_node_index_)
                );
                finishFailure(composed.status, detail);
                return true;
            }

            ++level_report.num_completed;
            index_.super_summaries_[static_cast<std::size_t>(super_level_ - 1U)]
                                   [super_node_index_] = std::move(composed);

            progress_.current_level = super_level_;
            progress_.current_node_index = super_node_index_;
            ++super_node_index_;
            advanceWork();
            return false;
        }
        case HBrickBuildStage::Idle:
        case HBrickBuildStage::Finished:
            return true;
    }

    return true;
}

void HBrickIndexBuilder::cancel() noexcept {
    cancelled_ = true;
}

bool HBrickIndexBuilder::running() const noexcept {
    return phase_ != HBrickBuildStage::Idle && phase_ != HBrickBuildStage::Finished;
}

const HBrickBuildProgress& HBrickIndexBuilder::progress() const noexcept {
    return progress_;
}

const HBrickBuildReport& HBrickIndexBuilder::report() const noexcept {
    return report_;
}

HBrickIndex HBrickIndexBuilder::takeIndex() {
    HBrickIndex result = std::move(index_);
    phase_ = HBrickBuildStage::Idle;
    progress_ = HBrickBuildProgress{};
    finalize_substep_ = 0U;
    seam_vertex_cursor_ = 0U;
    port_graph_tile_cursor_ = 0U;
    port_graph_builder_.reset();
    graph_ = nullptr;
    layout_ = nullptr;
    return result;
}

void HBrickIndexBuilder::finishFailure(
    const BaselineStatus status,
    const char* detail
) noexcept {
    index_.status_ = status;
    report_.valid = true;
    report_.status = status;
    report_.status_detail = detail != nullptr ? detail : "";
    report_.total_nanoseconds = monotonicNowNanoseconds() - build_started_ns_;
    if (index_.brick_index_.status() == BaselineStatus::NotRun
        && tile_index_.decomposition_.numSlots() > 0U) {
        countBaseTileOutcomes(
            tile_index_,
            report_.num_base_completed,
            report_.num_base_skipped,
            report_.num_base_with_closure,
            report_.num_base_empty_impassable
        );
    }
    progress_.stage = HBrickBuildStage::Finished;
    phase_ = HBrickBuildStage::Finished;
}

void HBrickIndexBuilder::finishSuccess() noexcept {
    index_.status_ = BaselineStatus::Completed;
    report_.valid = true;
    report_.status = BaselineStatus::Completed;
    report_.num_hierarchy_levels = index_.hierarchy_.numLevels();
    report_.num_ports = index_.brick_index_.ports().numPorts();
    report_.num_seams =
        static_cast<uint32_t>(index_.brick_index_.seamEdges().size());
    const HBrickStorageEstimate storage = index_.estimateStorageBreakdown();
    report_.estimated_brick_storage_bytes = storage.brick_bytes;
    report_.estimated_hbrick_extra_storage_bytes = storage.hbrick_extra_bytes;
    report_.estimated_storage_bytes = storage.totalBytes();
    report_.level_graph_stats = index_.levelGraphStats();
    report_.total_nanoseconds = monotonicNowNanoseconds() - build_started_ns_;
    progress_.stage = HBrickBuildStage::Finished;
    phase_ = HBrickBuildStage::Finished;
}

void HBrickIndexBuilder::advanceWork() noexcept {
    ++progress_.work_completed;
    if (progress_.work_total > 0U
        && progress_.work_completed > progress_.work_total) {
        progress_.work_completed = progress_.work_total;
    }
}

}  // namespace hbrick
