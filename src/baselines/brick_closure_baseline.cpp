#include "hbrick/baselines/brick_closure_baseline.hpp"

#include <limits>

#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/bit/kleene_squaring_options.hpp"
#include "hbrick/graph/connected_components.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/kleene_squaring_bounds.hpp"
#include "hbrick/graph/scc_compressed_closure.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/port_index.hpp"
#include "hbrick/tile/tile_closure_util.hpp"

namespace hbrick {

namespace {

constexpr uint32_t kAdjacencyVerticesPerStep = 256U;

[[nodiscard]] ReachabilityAnswer queryWithPortClosure(
    const BrickIndex& index,
    const BitMatrix& port_closure,
    const uint32_t source,
    const uint32_t target,
    BitVector& reachable_ports_scratch
) noexcept {
    const BrickTileIndex& tiles = index.tiles();
    const PortIndex& ports = index.ports();

    const uint32_t source_tile = tiles.tileIndexForGlobalVertex(source);
    const uint32_t target_tile = tiles.tileIndexForGlobalVertex(target);
    const uint32_t source_local = tiles.localIndexForGlobalVertex(source);
    const uint32_t target_local = tiles.localIndexForGlobalVertex(target);

    if (source_tile == std::numeric_limits<uint32_t>::max()
        || target_tile == std::numeric_limits<uint32_t>::max()
        || source_local == std::numeric_limits<uint32_t>::max()
        || target_local == std::numeric_limits<uint32_t>::max()) {
        return ReachabilityAnswer::Unreachable;
    }

    const BaseTileSummary& source_summary = tiles.summaryByIndex(source_tile);
    const BaseTileSummary& target_summary = tiles.summaryByIndex(target_tile);

    if (source_tile == target_tile
        && source_summary.local_closure.test(source_local, target_local)) {
        return ReachabilityAnswer::Reachable;
    }

    const uint32_t num_ports = ports.numPorts();
    if (num_ports == 0U || port_closure.numRows() != num_ports
        || port_closure.numCols() != num_ports) {
        return ReachabilityAnswer::Unreachable;
    }

    reachable_ports_scratch.clear();

    for (uint32_t src_tile_port = 0U; src_tile_port < source_summary.numPorts();
         ++src_tile_port) {
        if (!source_summary.vertex_to_boundary.test(source_local, src_tile_port)) {
            continue;
        }

        const uint32_t src_port_id =
            ports.portIdForTilePort(source_tile, src_tile_port);
        if (src_port_id == std::numeric_limits<uint32_t>::max()
            || src_port_id >= num_ports) {
            continue;
        }

        reachable_ports_scratch.rowOr(port_closure.row(src_port_id));
    }

    for (uint32_t dst_tile_port = 0U; dst_tile_port < target_summary.numPorts();
         ++dst_tile_port) {
        if (!target_summary.boundary_to_vertex.test(dst_tile_port, target_local)) {
            continue;
        }

        const uint32_t dst_port_id =
            ports.portIdForTilePort(target_tile, dst_tile_port);
        if (dst_port_id == std::numeric_limits<uint32_t>::max()
            || dst_port_id >= num_ports) {
            continue;
        }

        if (reachable_ports_scratch.test(dst_port_id)) {
            return ReachabilityAnswer::Reachable;
        }
    }

    return ReachabilityAnswer::Unreachable;
}

}  // namespace

void BrickClosureBaseline::resetPreprocessState() noexcept {
    index_builder_.cancel();
    preprocess_phase_ = PreprocessPhase::Idle;
    preprocess_work_completed_ = 0U;
    preprocess_work_total_ = 0U;
    adjacency_vertex_cursor_ = 0U;
    kleene_rounds_remaining_ = 0U;
    kleene_rounds_total_ = 0U;
    kleene_rounds_executed_ = 0U;
    kleene_scc_compression_tried_ = false;
    query_reachable_ports_ = BitVector{};
    num_vertices_ = 0U;
    memory_ledger_.reset(std::numeric_limits<uint64_t>::max());
}

void BrickClosureBaseline::beginPreprocess(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSize nominal_tile_size,
    const uint64_t max_memory_bytes,
    const KleeneSquaringOptions kleene_options
) {
    status_ = BaselineStatus::NotRun;
    index_ = BrickIndex{};
    port_closure_ = BitMatrix{};
    port_closure_scratch_ = BitMatrix{};
    resetPreprocessState();

    num_vertices_ = graph.numVertices();

    kleene_options_ = kleene_options;
    kleene_thread_count_ = resolveKleeneThreadCount(kleene_options_);
    memory_ledger_.reset(max_memory_bytes);
    index_builder_.begin(
        graph,
        layout,
        nominal_tile_size,
        max_memory_bytes,
        &memory_ledger_
    );
    if (!index_builder_.running()) {
        status_ = index_builder_.report().valid
            ? index_builder_.report().status
            : BaselineStatus::Failed;
        return;
    }

    preprocess_phase_ = PreprocessPhase::IndexBuild;
    preprocess_work_total_ = index_builder_.progress().work_total + 1ULL;
}

void BrickClosureBaseline::beginAdjacencyBuild() noexcept {
    const uint32_t num_ports = index_.ports().numPorts();
    const uint64_t port_matrix_bytes = exactBitMatrixStorageBytes(num_ports, num_ports);
    const uint64_t query_vector_bytes =
        BitVector::wordCount(static_cast<size_t>(num_ports)) * sizeof(uint64_t);
    if (!memory_ledger_.tryCharge(port_matrix_bytes * 2U + query_vector_bytes)) {
        status_ = BaselineStatus::SkippedByPolicy;
        preprocess_phase_ = PreprocessPhase::Done;
        return;
    }

    port_closure_ = BitMatrix(num_ports, num_ports);
    port_closure_scratch_ = BitMatrix(num_ports, num_ports);
    query_reachable_ports_ = BitVector(static_cast<size_t>(num_ports));
    adjacency_vertex_cursor_ = 0U;
    preprocess_phase_ = PreprocessPhase::AdjacencyBuild;
    preprocess_work_total_ += static_cast<uint64_t>(num_ports);
}

void BrickClosureBaseline::runAdjacencyBatch() noexcept {
    const CsrGraph& port_graph = index_.portGraph();
    const uint32_t num_ports = port_graph.numVertices();
    const uint32_t vertex_end = std::min(
        adjacency_vertex_cursor_ + kAdjacencyVerticesPerStep,
        num_ports
    );

    for (uint32_t vertex = adjacency_vertex_cursor_; vertex < vertex_end; ++vertex) {
        port_closure_.set(vertex, vertex);
        for (const uint32_t neighbor : port_graph.outNeighbors(vertex)) {
            port_closure_.set(vertex, neighbor);
        }
        ++preprocess_work_completed_;
    }

    adjacency_vertex_cursor_ = vertex_end;
    if (adjacency_vertex_cursor_ >= num_ports) {
        beginKleeneClosure();
    }
}

void BrickClosureBaseline::beginKleeneClosure() noexcept {
    GraphSearchScratch scc_scratch{index_.portGraph().numVertices()};
    kleene_rounds_total_ =
        kleeneSquaringCountForCsrGraph(index_.portGraph(), scc_scratch);
    kleene_rounds_remaining_ = kleene_rounds_total_;
    preprocess_work_total_ += static_cast<uint64_t>(kleene_rounds_total_);
    preprocess_phase_ = PreprocessPhase::KleeneClosure;

    if (kleene_rounds_remaining_ == 0U) {
        status_ = BaselineStatus::Completed;
        preprocess_phase_ = PreprocessPhase::Done;
    }
}

bool BrickClosureBaseline::runKleeneStep() noexcept {
    if (kleene_rounds_remaining_ == 0U) {
        status_ = BaselineStatus::Completed;
        preprocess_phase_ = PreprocessPhase::Done;
        return true;
    }

    if (!kleene_scc_compression_tried_) {
        kleene_scc_compression_tried_ = true;
        GraphSearchScratch scc_scratch{index_.portGraph().numVertices()};
        if (transitiveClosureKleeneSccCompressedInPlace(
                port_closure_,
                index_.portGraph(),
                scc_scratch,
                &port_closure_scratch_,
                kleene_options_)) {
            kleene_rounds_executed_ = 0U;
            preprocess_work_completed_ += static_cast<uint64_t>(kleene_rounds_remaining_);
            kleene_rounds_remaining_ = 0U;
            status_ = BaselineStatus::Completed;
            preprocess_phase_ = PreprocessPhase::Done;
            return true;
        }
    }

    const bool fixpoint = BooleanClosure::transitiveClosureKleeneSquaringStepInPlace(
        port_closure_,
        port_closure_scratch_,
        kleene_options_
    );
    ++kleene_rounds_executed_;
    ++preprocess_work_completed_;
    if (fixpoint) {
        kleene_rounds_remaining_ = 0U;
        status_ = BaselineStatus::Completed;
        preprocess_phase_ = PreprocessPhase::Done;
        return true;
    }

    --kleene_rounds_remaining_;
    if (kleene_rounds_remaining_ == 0U) {
        status_ = BaselineStatus::Completed;
        preprocess_phase_ = PreprocessPhase::Done;
        return true;
    }

    return false;
}

bool BrickClosureBaseline::preprocessStep() noexcept {
    switch (preprocess_phase_) {
        case PreprocessPhase::Idle:
        case PreprocessPhase::Done:
            return true;
        case PreprocessPhase::IndexBuild: {
            if (!index_builder_.running()) {
                status_ = index_builder_.report().valid
                    ? index_builder_.report().status
                    : BaselineStatus::Failed;
                preprocess_phase_ = PreprocessPhase::Done;
                return true;
            }

            const bool index_finished = index_builder_.step();
            preprocess_work_completed_ = index_builder_.progress().work_completed;
            preprocess_work_total_ = std::max<uint64_t>(
                preprocess_work_total_,
                index_builder_.progress().work_total + 1ULL
            );

            if (!index_builder_.running()) {
                index_ = index_builder_.takeIndex();
                status_ = index_.status();
                if (status_ != BaselineStatus::Completed) {
                    preprocess_phase_ = PreprocessPhase::Done;
                    return true;
                }
                beginAdjacencyBuild();
                if (preprocess_phase_ == PreprocessPhase::Done) {
                    return true;
                }
                return false;
            }

            return index_finished;
        }
        case PreprocessPhase::AdjacencyBuild:
            runAdjacencyBatch();
            return preprocess_phase_ == PreprocessPhase::Done;
        case PreprocessPhase::KleeneClosure:
            return runKleeneStep();
    }

    return true;
}

void BrickClosureBaseline::cancelPreprocess() noexcept {
    resetPreprocessState();
    status_ = BaselineStatus::NotRun;
}

bool BrickClosureBaseline::preprocessActive() const noexcept {
    return preprocess_phase_ != PreprocessPhase::Idle
        && preprocess_phase_ != PreprocessPhase::Done;
}

void BrickClosureBaseline::preprocess(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSize nominal_tile_size,
    const uint64_t max_memory_bytes,
    const KleeneSquaringOptions kleene_options
) {
    beginPreprocess(graph, layout, nominal_tile_size, max_memory_bytes, kleene_options);
    while (!preprocessStep()) {
    }
}

ReachabilityAnswer BrickClosureBaseline::query(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source >= num_vertices_ || target >= num_vertices_) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source == target) {
        return ReachabilityAnswer::Reachable;
    }

    return queryWithPortClosure(
        index_,
        port_closure_,
        source,
        target,
        query_reachable_ports_
    );
}

uint64_t BrickClosureBaseline::indexStorageBytes() const noexcept {
    return memory_ledger_.chargedBytes();
}

}  // namespace hbrick
