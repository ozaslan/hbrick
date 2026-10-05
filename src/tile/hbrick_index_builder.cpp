#include "hbrick/tile/hbrick_index_builder.hpp"

#include <algorithm>
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
#include "hbrick/tile/tile_decomposition.hpp"

namespace hbrick {

namespace {

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
            return "super-tile skipped (no composable child boundaries)";
        case BaselineStatus::OutOfMemory:
            return "super-tile exceeded the memory budget";
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
    config_ = config;
    super_tile_ledger_.reset(config.max_memory_bytes);
    index_ = HBrickIndex{};
    index_.config_ = config;
    index_.status_ = BaselineStatus::NotRun;
    brick_index_builder_ = BrickIndexBuilder{};
    decomposition_ = TileDecomposition{};
    progress_ = HBrickBuildProgress{};
    report_ = HBrickBuildReport{};
    phase_ = HBrickBuildStage::Idle;
    super_level_ = 1U;
    super_node_index_ = 0U;
    total_super_regions_ = 0U;
    cancelled_ = false;
    build_started_ns_ = monotonicNowNanoseconds();
    phase_started_ns_ = build_started_ns_;
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

    brick_index_builder_.begin(
        graph,
        layout,
        config.base_tile_size,
        config.max_memory_bytes,
        nullptr,
        true,
        config.omit_local_closure
    );
    if (!brick_index_builder_.running()) {
        finishFailure(
            brick_index_builder_.report().valid
                ? brick_index_builder_.report().status
                : BaselineStatus::Failed,
            baseTileFailureDetail(
                brick_index_builder_.report().valid
                    ? brick_index_builder_.report().status
                    : BaselineStatus::Failed
            )
        );
        return;
    }

    report_.num_base_tiles = decomposition_.numSlots();
    progress_.num_base_tiles = decomposition_.numSlots();
    progress_.stage = HBrickBuildStage::BaseTiles;
    progress_.work_total = brick_index_builder_.progress().work_total + 2U;
    phase_ = HBrickBuildStage::BaseTiles;
}

bool HBrickIndexBuilder::step() noexcept {
    if (phase_ == HBrickBuildStage::Idle || phase_ == HBrickBuildStage::Finished) {
        return true;
    }
    if (cancelled_) {
        finishFailure(BaselineStatus::Failed, "build cancelled");
        return true;
    }

    try {
        switch (phase_) {
            case HBrickBuildStage::BaseTiles:
            case HBrickBuildStage::FinalizeBrick: {
            if (!brick_index_builder_.running()) {
                // BrickIndexBuilder::begin() failed or was cancelled before
                // entering the step loop.  Report the failure and stop.
                const BrickIndexBuildReport& brick_report = brick_index_builder_.report();
                if (!brick_report.valid) {
                    finishFailure(BaselineStatus::Failed, "brick index build did not start");
                } else {
                    finishFailure(brick_report.status,
                                  baseTileFailureDetail(brick_report.status));
                }
                return true;
            }

            (void)brick_index_builder_.step();
            const BrickIndexBuildProgress& brick_progress = brick_index_builder_.progress();
            progress_.work_completed = brick_progress.work_completed;
            progress_.current_base_tile_index = brick_progress.current_base_tile_index;

            if (!brick_index_builder_.running()) {
                const BrickIndexBuildReport& brick_report = brick_index_builder_.report();
                index_.brick_index_ = brick_index_builder_.takeIndex();
                report_.base_tile_nanoseconds = brick_report.base_tile_nanoseconds;
                report_.base_tile_closure_nanoseconds =
                    brick_report.base_tile_closure_nanoseconds;
                report_.brick_finalize_nanoseconds = brick_report.finalize_nanoseconds;
                report_.num_base_completed = brick_report.num_base_completed;
                report_.num_base_skipped = brick_report.num_base_skipped;
                report_.num_ports = brick_report.num_ports;
                report_.num_seams = brick_report.num_seams;
                countBaseTileOutcomes(
                    index_.brick_index_.tiles(),
                    report_.num_base_completed,
                    report_.num_base_skipped,
                    report_.num_base_with_closure,
                    report_.num_base_empty_impassable
                );

                if (index_.brick_index_.status() != BaselineStatus::Completed) {
                    finishFailure(
                        index_.brick_index_.status(),
                        baseTileFailureDetail(index_.brick_index_.status())
                    );
                    return true;
                }

                phase_ = HBrickBuildStage::Hierarchy;
                phase_started_ns_ = monotonicNowNanoseconds();
                advanceWork();
                return false;
            }

            return false;
        }
        case HBrickBuildStage::Hierarchy: {
            index_.hierarchy_ = HierarchyTree::build(
                index_.brick_index_.tiles().decomposition(),
                config_.group_size,
                config_.max_depth
            );
            report_.hierarchy_nanoseconds =
                monotonicNowNanoseconds() - phase_started_ns_;
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

            super_tile_ledger_.reset(config_.max_memory_bytes);
            uint64_t retained_container_bytes =
                static_cast<uint64_t>(index_.super_summaries_.capacity())
                * sizeof(std::vector<SuperTileSummary>);
            for (const auto& level_summaries : index_.super_summaries_) {
                retained_container_bytes +=
                    static_cast<uint64_t>(level_summaries.capacity()) * sizeof(SuperTileSummary);
            }
            retained_container_bytes +=
                static_cast<uint64_t>(report_.super_levels.capacity())
                * sizeof(HBrickSuperLevelReport);
            if (!super_tile_ledger_.tryCharge(
                    index_.brick_index_.measureStorageBytes()
                    + index_.hierarchy_.measureStorageBytes()
                    + retained_container_bytes
                )) {
                finishFailure(BaselineStatus::OutOfMemory, "base brick index exceeded memory cap");
                return true;
            }

            super_level_ = 1U;
            super_node_index_ = 0U;
            super_compose_started_ns_ = monotonicNowNanoseconds();

            const std::span<const SeamEdge> all_seams = index_.brick_index_.seamEdges();
            const PortIndex& port_index = index_.brick_index_.ports();
            const uint32_t num_base_slots = index_.hierarchy_.baseDecomposition().numSlots();

            seam_offsets_.assign(num_base_slots + 1U, 0U);
            for (const SeamEdge& seam : all_seams) {
                const uint32_t tile_idx = port_index.port(seam.from_port_id).tile_index;
                if (tile_idx < num_base_slots) {
                    ++seam_offsets_[tile_idx + 1U];
                }
            }
            for (uint32_t i = 0U; i < num_base_slots; ++i) {
                seam_offsets_[i + 1U] += seam_offsets_[i];
            }
            flat_seams_.resize(seam_offsets_.back());
            std::vector<uint32_t> insert_cursor = seam_offsets_;
            for (const SeamEdge& seam : all_seams) {
                const uint32_t tile_idx = port_index.port(seam.from_port_id).tile_index;
                if (tile_idx < num_base_slots) {
                    flat_seams_[insert_cursor[tile_idx]++] = seam;
                }
            }

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

            local_seam_scratch_.clear();
            const TileDecomposition& base_decomp = index_.hierarchy_.baseDecomposition();
            if (base_decomp.nominalSize().width > 0U && base_decomp.nominalSize().height > 0U) {
                const uint32_t min_bx = parent_slot.origin.x / base_decomp.nominalSize().width;
                const uint32_t max_bx = (parent_slot.maxX() == 0U) ? 0U : (parent_slot.maxX() - 1U) / base_decomp.nominalSize().width;
                const uint32_t min_by = parent_slot.origin.y / base_decomp.nominalSize().height;
                const uint32_t max_by = (parent_slot.maxY() == 0U) ? 0U : (parent_slot.maxY() - 1U) / base_decomp.nominalSize().height;

                for (uint32_t by = min_by; by <= max_by && by < base_decomp.numSlotsJ(); ++by) {
                    for (uint32_t bx = min_bx; bx <= max_bx && bx < base_decomp.numSlotsI(); ++bx) {
                        const uint32_t tile_idx = by * base_decomp.numSlotsI() + bx;
                        if (tile_idx + 1U < seam_offsets_.size()) {
                            const uint32_t start = seam_offsets_[tile_idx];
                            const uint32_t end = seam_offsets_[tile_idx + 1U];
                            if (end > start) {
                                local_seam_scratch_.insert(
                                    local_seam_scratch_.end(),
                                    flat_seams_.begin() + start,
                                    flat_seams_.begin() + end
                                );
                            }
                        }
                    }
                }
            }

            const uint64_t node_started_ns = monotonicNowNanoseconds();
            SuperTileSummary composed = composeSuperTile(
                parent_slot,
                children,
                index_.brick_index_.ports(),
                local_seam_scratch_,
                config_.max_memory_bytes,
                &super_tile_ledger_,
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
            const uint32_t gamma =
                static_cast<uint32_t>(composed.interface_closure.numRows());
            level_report.gamma_sum += gamma;
            level_report.gamma_max = std::max(level_report.gamma_max, gamma);
            ++level_report.gamma_samples;
            if (gamma > 0U) {
                const double denom =
                    static_cast<double>(gamma) * static_cast<double>(gamma);
                level_report.density_sum +=
                    static_cast<double>(composed.interface_closure.popcount()) / denom;
            }
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
    } catch (const std::bad_alloc&) {
        finishFailure(BaselineStatus::OutOfMemory, "out of memory");
        return true;
    } catch (const std::exception& e) {
        finishFailure(BaselineStatus::Failed, e.what());
        return true;
    }

    return true;
}

void HBrickIndexBuilder::cancel() noexcept {
    cancelled_ = true;
    brick_index_builder_.cancel();
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
    seam_offsets_.clear();
    seam_offsets_.shrink_to_fit();
    flat_seams_.clear();
    flat_seams_.shrink_to_fit();
    local_seam_scratch_.clear();
    local_seam_scratch_.shrink_to_fit();
    index_.build_report_ = report_;
    HBrickIndex result = std::move(index_);
    phase_ = HBrickBuildStage::Idle;
    progress_ = HBrickBuildProgress{};
    brick_index_builder_ = BrickIndexBuilder{};
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
        && index_.brick_index_.tiles().decomposition().numSlots() > 0U) {
        countBaseTileOutcomes(
            index_.brick_index_.tiles(),
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
    index_.updateHierarchyQuerySound();
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
