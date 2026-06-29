#include "hbrick/tile/brick_index_builder.hpp"

#include <chrono>
#include <limits>
#include <utility>

#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/port_graph.hpp"
#include "hbrick/tile/port_index.hpp"
#include "hbrick/tile/tile_closure_util.hpp"

namespace hbrick {

namespace {

constexpr uint32_t kSeamVerticesPerStep = 131072U;
constexpr uint32_t kPortGraphTilesPerStep = 64U;

[[nodiscard]] uint64_t monotonicNowNanoseconds() noexcept {
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock::now().time_since_epoch()
    ).count());
}

void countBaseTileOutcomes(
    const BrickTileIndex& tile_index,
    uint32_t& completed,
    uint32_t& skipped
) noexcept {
    completed = 0U;
    skipped = 0U;
    for (const BaseTileSummary& summary : tile_index.summaries()) {
        if (summary.status == BaselineStatus::Completed) {
            ++completed;
        } else if (summary.status == BaselineStatus::SkippedByPolicy) {
            ++skipped;
        }
    }
}

[[nodiscard]] uint64_t estimateWorkTotal(
    const uint32_t num_slots,
    const uint32_t num_vertices
) noexcept {
    const uint64_t seam_chunks =
        (static_cast<uint64_t>(num_vertices) + kSeamVerticesPerStep - 1ULL)
        / static_cast<uint64_t>(kSeamVerticesPerStep);
    const uint64_t port_chunks =
        (static_cast<uint64_t>(num_slots) + kPortGraphTilesPerStep - 1ULL)
        / static_cast<uint64_t>(kPortGraphTilesPerStep);
    return static_cast<uint64_t>(num_slots) + seam_chunks + port_chunks + 1ULL;
}

}  // namespace

void BrickIndexBuilder::begin(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSize nominal_tile_size,
    const uint64_t max_memory_bytes,
    PreprocessMemoryLedger* shared_ledger
) {
    graph_ = &graph;
    layout_ = &layout;
    if (shared_ledger != nullptr) {
        ledger_ = shared_ledger;
    } else {
        owned_ledger_.reset(max_memory_bytes);
        ledger_ = &owned_ledger_;
    }
    index_ = BrickIndex{};
    index_.status_ = BaselineStatus::NotRun;
    tile_index_ = BrickTileIndex{};
    decomposition_ = TileDecomposition{};
    progress_ = BrickIndexBuildProgress{};
    report_ = BrickIndexBuildReport{};
    phase_ = BrickIndexBuildStage::Idle;
    next_base_tile_index_ = 0U;
    finalize_substep_ = static_cast<uint8_t>(FinalizeSubstep::PortIndex);
    seam_vertex_cursor_ = 0U;
    port_graph_tile_cursor_ = 0U;
    port_graph_builder_.reset();
    cancelled_ = false;
    base_tile_started_ns_ = 0U;
    finalize_started_ns_ = 0U;
    closure_scratch_charged_bytes_ = 0U;
    port_graph_pending_charged_bytes_ = 0U;
    base_tile_closure_scratch_ = BitMatrix{};

    if (!nominal_tile_size.isValid()) {
        finishFailure(BaselineStatus::Failed);
        return;
    }

    decomposition_ = TileDecomposition::decompose(
        graph.width(),
        graph.height(),
        nominal_tile_size
    );
    tile_index_.decomposition_ = decomposition_;
    const uint64_t summaries_bytes =
        static_cast<uint64_t>(decomposition_.numSlots()) * sizeof(BaseTileSummary);
    if (!ledger_->tryCharge(summaries_bytes)) {
        finishFailure(BaselineStatus::SkippedByPolicy);
        return;
    }
    tile_index_.summaries_.resize(decomposition_.numSlots());

    const uint64_t lookup_bytes =
        static_cast<uint64_t>(graph.numVertices()) * sizeof(uint32_t) * 2U;
    if (!ledger_->tryCharge(lookup_bytes)) {
        ledger_->releaseCharge(summaries_bytes);
        finishFailure(BaselineStatus::SkippedByPolicy);
        return;
    }

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
    progress_.work_total = estimateWorkTotal(
        decomposition_.numSlots(),
        graph.numVertices()
    );
    progress_.stage = BrickIndexBuildStage::BaseTiles;
    phase_ = BrickIndexBuildStage::BaseTiles;
    base_tile_started_ns_ = monotonicNowNanoseconds();
}

bool BrickIndexBuilder::step() noexcept {
    if (phase_ == BrickIndexBuildStage::Idle
        || phase_ == BrickIndexBuildStage::Finished) {
        return true;
    }
    if (cancelled_) {
        finishFailure(BaselineStatus::Failed);
        return true;
    }

    switch (phase_) {
        case BrickIndexBuildStage::BaseTiles: {
            if (next_base_tile_index_ >= decomposition_.numSlots()) {
                report_.base_tile_nanoseconds =
                    monotonicNowNanoseconds() - base_tile_started_ns_;
                countBaseTileOutcomes(
                    tile_index_,
                    report_.num_base_completed,
                    report_.num_base_skipped
                );
                if (!tile_index_.allTilesCompleted()) {
                    finishFailure(BaselineStatus::SkippedByPolicy);
                    return true;
                }
                phase_ = BrickIndexBuildStage::Finalize;
                progress_.stage = BrickIndexBuildStage::Finalize;
                finalize_started_ns_ = monotonicNowNanoseconds();
                return false;
            }

            const TileSlot& slot = decomposition_.slotByIndex(next_base_tile_index_);
            uint64_t tile_closure_ns = 0U;
            BaseTileSummary summary = buildBaseTile(
                *graph_,
                *layout_,
                slot,
                *ledger_,
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

            if (!chargeClosureScratchGrowth()) {
                finishFailure(BaselineStatus::SkippedByPolicy);
                return true;
            }

            progress_.current_base_tile_index = next_base_tile_index_;
            ++next_base_tile_index_;
            advanceWork();
            return false;
        }
        case BrickIndexBuildStage::Finalize: {
            switch (static_cast<FinalizeSubstep>(finalize_substep_)) {
                case FinalizeSubstep::PortIndex: {
                    index_.tile_index_ = std::move(tile_index_);
                    tile_index_ = BrickTileIndex{};
                    if (!index_.tile_index_.allTilesCompleted()) {
                        finishFailure(BaselineStatus::SkippedByPolicy);
                        return true;
                    }

                    const uint64_t port_index_bytes = PortIndex::estimateBuildStorageBytes(
                        index_.tile_index_,
                        graph_->numVertices()
                    );
                    if (!ledger_->tryCharge(port_index_bytes)) {
                        finishFailure(BaselineStatus::SkippedByPolicy);
                        return true;
                    }
                    index_.port_index_ = PortIndex::build(
                        index_.tile_index_,
                        graph_->numVertices()
                    );

                    index_.seam_edges_.clear();
                    seam_deduper_.clear();
                    seam_vertex_cursor_ = 0U;
                    finalize_substep_ = static_cast<uint8_t>(FinalizeSubstep::SeamEdges);
                    advanceWork();
                    return false;
                }
                case FinalizeSubstep::SeamEdges: {
                    const uint32_t vertex_end = std::min(
                        seam_vertex_cursor_ + kSeamVerticesPerStep,
                        graph_->numVertices()
                    );
                    if (!collectSeamEdgesForVertexRange(
                            graph_->csrGraph(),
                            index_.tile_index_,
                            index_.port_index_,
                            seam_vertex_cursor_,
                            vertex_end,
                            index_.seam_edges_,
                            &seam_deduper_,
                            ledger_
                        )) {
                        finishFailure(BaselineStatus::SkippedByPolicy);
                        return true;
                    }
                    seam_vertex_cursor_ = vertex_end;
                    if (seam_vertex_cursor_ < graph_->numVertices()) {
                        advanceWork();
                        return false;
                    }

                    port_graph_tile_cursor_ = 0U;
                    port_graph_builder_.emplace(index_.port_index_.numPorts());
                    finalize_substep_ = static_cast<uint8_t>(FinalizeSubstep::PortGraph);
                    advanceWork();
                    return false;
                }
                case FinalizeSubstep::PortGraph: {
                    if (!port_graph_builder_.has_value()) {
                        finishFailure(BaselineStatus::Failed);
                        return true;
                    }

                    const uint32_t num_tiles = index_.tile_index_.decomposition().numSlots();
                    const uint32_t tile_end = std::min(
                        port_graph_tile_cursor_ + kPortGraphTilesPerStep,
                        num_tiles
                    );
                    for (uint32_t tile_index_value = port_graph_tile_cursor_;
                         tile_index_value < tile_end;
                         ++tile_index_value) {
                        addIntraTilePortEdgesForTile(
                            tile_index_value,
                            index_.tile_index_,
                            index_.port_index_,
                            *port_graph_builder_
                        );
                    }
                    if (!chargePortGraphPendingEdges()) {
                        finishFailure(BaselineStatus::SkippedByPolicy);
                        return true;
                    }
                    port_graph_tile_cursor_ = tile_end;
                    if (port_graph_tile_cursor_ < num_tiles) {
                        advanceWork();
                        return false;
                    }

                    addSeamEdgesToPortGraph(*port_graph_builder_, index_.seam_edges_);
                    if (!chargePortGraphPendingEdges()) {
                        finishFailure(BaselineStatus::SkippedByPolicy);
                        return true;
                    }
                    const uint64_t port_graph_bytes =
                        port_graph_builder_->estimateBuiltStorageBytes();
                    if (!ledger_->tryCharge(port_graph_bytes)) {
                        finishFailure(BaselineStatus::SkippedByPolicy);
                        return true;
                    }
                    index_.port_graph_ = port_graph_builder_->build();
                    ledger_->releaseCharge(port_graph_pending_charged_bytes_);
                    port_graph_pending_charged_bytes_ = 0U;
                    port_graph_builder_.reset();
                    index_.status_ = BaselineStatus::Completed;
                    finishSuccess();
                    return true;
                }
            }

            finishFailure(BaselineStatus::Failed);
            return true;
        }
        case BrickIndexBuildStage::Idle:
        case BrickIndexBuildStage::Finished:
            return true;
    }

    return true;
}

void BrickIndexBuilder::cancel() noexcept {
    cancelled_ = true;
}

bool BrickIndexBuilder::running() const noexcept {
    return phase_ != BrickIndexBuildStage::Idle
        && phase_ != BrickIndexBuildStage::Finished;
}

const BrickIndexBuildProgress& BrickIndexBuilder::progress() const noexcept {
    return progress_;
}

const BrickIndexBuildReport& BrickIndexBuilder::report() const noexcept {
    return report_;
}

BrickIndex BrickIndexBuilder::takeIndex() {
    BrickIndex result = std::move(index_);
    phase_ = BrickIndexBuildStage::Idle;
    progress_ = BrickIndexBuildProgress{};
    finalize_substep_ = 0U;
    seam_vertex_cursor_ = 0U;
    port_graph_tile_cursor_ = 0U;
    port_graph_builder_.reset();
    seam_deduper_.clear();
    ledger_ = nullptr;
    graph_ = nullptr;
    layout_ = nullptr;
    return result;
}

uint64_t BrickIndexBuilder::chargedStorageBytes() const noexcept {
    return ledger_ != nullptr ? ledger_->chargedBytes() : 0U;
}

void BrickIndexBuilder::adoptPartialBaseTiles() noexcept {
    if (tile_index_.decomposition_.numSlots() == 0U) {
        return;
    }
    if (index_.tile_index_.decomposition_.numSlots() == 0U) {
        index_.tile_index_ = std::move(tile_index_);
        tile_index_ = BrickTileIndex{};
    }
}

bool BrickIndexBuilder::chargeClosureScratchGrowth() noexcept {
    if (ledger_ == nullptr) {
        return true;
    }

    const uint64_t scratch_bytes = exactBitMatrixStorageBytes(
        base_tile_closure_scratch_.numRows(),
        base_tile_closure_scratch_.numCols()
    );
    if (scratch_bytes <= closure_scratch_charged_bytes_) {
        return true;
    }

    const uint64_t delta = scratch_bytes - closure_scratch_charged_bytes_;
    if (!ledger_->tryCharge(delta)) {
        return false;
    }
    closure_scratch_charged_bytes_ = scratch_bytes;
    return true;
}

bool BrickIndexBuilder::chargePortGraphPendingEdges() noexcept {
    if (ledger_ == nullptr || !port_graph_builder_.has_value()) {
        return true;
    }

    const uint64_t pending_bytes = port_graph_builder_->estimatePendingStorageBytes();
    if (pending_bytes <= port_graph_pending_charged_bytes_) {
        return true;
    }

    const uint64_t delta = pending_bytes - port_graph_pending_charged_bytes_;
    if (!ledger_->tryCharge(delta)) {
        return false;
    }
    port_graph_pending_charged_bytes_ = pending_bytes;
    return true;
}

void BrickIndexBuilder::releaseClosureScratchCharge() noexcept {
    if (ledger_ == nullptr || closure_scratch_charged_bytes_ == 0U) {
        return;
    }

    ledger_->releaseCharge(closure_scratch_charged_bytes_);
    closure_scratch_charged_bytes_ = 0U;
}

void BrickIndexBuilder::releaseFinalizeStorage() noexcept {
    if (ledger_ == nullptr) {
        return;
    }

    const uint64_t port_index_bytes = index_.port_index_.estimateStorageBytes();
    if (port_index_bytes > 0U) {
        ledger_->releaseCharge(port_index_bytes);
        index_.port_index_ = PortIndex{};
    }

    const uint64_t seam_bytes =
        static_cast<uint64_t>(index_.seam_edges_.size()) * sizeof(SeamEdge);
    if (seam_bytes > 0U) {
        ledger_->releaseCharge(seam_bytes);
        index_.seam_edges_.clear();
    }

    const uint64_t port_graph_bytes = index_.port_graph_.estimateStorageBytes();
    if (port_graph_bytes > 0U) {
        ledger_->releaseCharge(port_graph_bytes);
        index_.port_graph_ = CsrGraph{};
    }

    if (port_graph_pending_charged_bytes_ > 0U) {
        ledger_->releaseCharge(port_graph_pending_charged_bytes_);
        port_graph_pending_charged_bytes_ = 0U;
    }

    port_graph_builder_.reset();
    seam_deduper_.clear();
}

void BrickIndexBuilder::finishFailure(const BaselineStatus status) noexcept {
    releaseFinalizeStorage();
    adoptPartialBaseTiles();
    releaseClosureScratchCharge();
    index_.status_ = status;
    index_.storage_bytes_ = ledger_ != nullptr ? ledger_->chargedBytes() : 0U;
    report_.valid = true;
    report_.status = status;
    if (report_.num_base_tiles == 0U && tile_index_.decomposition_.numSlots() > 0U) {
        report_.num_base_tiles = tile_index_.decomposition_.numSlots();
        countBaseTileOutcomes(
            tile_index_,
            report_.num_base_completed,
            report_.num_base_skipped
        );
    }
    progress_.stage = BrickIndexBuildStage::Finished;
    phase_ = BrickIndexBuildStage::Finished;
}

void BrickIndexBuilder::finishSuccess() noexcept {
    report_.valid = true;
    report_.status = BaselineStatus::Completed;
    report_.finalize_nanoseconds = monotonicNowNanoseconds() - finalize_started_ns_;
    report_.num_ports = index_.port_index_.numPorts();
    report_.num_seams = static_cast<uint32_t>(index_.seam_edges_.size());
    releaseClosureScratchCharge();
    index_.storage_bytes_ = ledger_ != nullptr ? ledger_->chargedBytes() : 0U;
    progress_.stage = BrickIndexBuildStage::Finished;
    phase_ = BrickIndexBuildStage::Finished;
}

void BrickIndexBuilder::advanceWork() noexcept {
    ++progress_.work_completed;
    if (progress_.work_total > 0U
        && progress_.work_completed > progress_.work_total) {
        progress_.work_completed = progress_.work_total;
    }
}

}  // namespace hbrick
