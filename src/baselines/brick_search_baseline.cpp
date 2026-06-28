#include "hbrick/baselines/brick_search_baseline.hpp"

#include <limits>

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/port_index.hpp"

namespace hbrick {

namespace {

ReachabilityAnswer queryPortBfsWithAttachments(
    const BrickIndex& index,
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) noexcept {
    const BrickTileIndex& tiles = index.tiles();
    const PortIndex& ports = index.ports();
    const CsrGraph& port_graph = index.portGraph();

    const uint32_t num_port_vertices = ports.numPorts();
    if (num_port_vertices == 0U) {
        return ReachabilityAnswer::Unreachable;
    }

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

    const uint32_t mark = scratch.nextMark();
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<uint32_t>& queue = scratch.queue();
    queue.clear();

    for (uint32_t tile_port_index = 0U; tile_port_index < source_summary.numPorts();
         ++tile_port_index) {
        if (!source_summary.vertex_to_boundary.test(source_local, tile_port_index)) {
            continue;
        }

        const uint32_t port_id =
            ports.portIdForTilePort(source_tile, tile_port_index);
        if (port_id == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        if (visited[port_id] == mark) {
            continue;
        }

        if (port_id < num_port_vertices
            && target_tile == ports.port(port_id).tile_index
            && target_summary.boundary_to_vertex.test(
                ports.port(port_id).tile_port_index,
                target_local
            )) {
            return ReachabilityAnswer::Reachable;
        }

        visited[port_id] = mark;
        queue.push_back(port_id);
    }

    if (queue.empty()) {
        return ReachabilityAnswer::Unreachable;
    }

    std::size_t head = 0U;
    while (head < queue.size()) {
        const uint32_t port_id = queue[head];
        ++head;

        for (const uint32_t neighbor : port_graph.outNeighbors(port_id)) {
            if (neighbor >= num_port_vertices || visited[neighbor] == mark) {
                continue;
            }

            if (target_tile == ports.port(neighbor).tile_index
                && target_summary.boundary_to_vertex.test(
                    ports.port(neighbor).tile_port_index,
                    target_local
                )) {
                return ReachabilityAnswer::Reachable;
            }

            visited[neighbor] = mark;
            queue.push_back(neighbor);
        }
    }

    return ReachabilityAnswer::Unreachable;
}

}  // namespace

void BrickSearchBaseline::resetPreprocessState() noexcept {
    index_builder_.cancel();
    preprocess_active_ = false;
    preprocess_work_completed_ = 0U;
    preprocess_work_total_ = 0U;
}

void BrickSearchBaseline::beginPreprocess(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSize nominal_tile_size,
    const uint64_t max_memory_bytes
) {
    status_ = BaselineStatus::NotRun;
    index_ = BrickIndex{};
    port_scratch_ = GraphSearchScratch{};
    resetPreprocessState();

    index_builder_.begin(graph, layout, nominal_tile_size, max_memory_bytes);
    if (!index_builder_.running()) {
        status_ = index_builder_.report().valid
            ? index_builder_.report().status
            : BaselineStatus::Failed;
        return;
    }

    preprocess_active_ = true;
    preprocess_work_total_ = index_builder_.progress().work_total;
}

bool BrickSearchBaseline::preprocessStep() noexcept {
    if (!preprocess_active_) {
        return true;
    }

    if (!index_builder_.running()) {
        preprocess_active_ = false;
        return true;
    }

    const bool finished = index_builder_.step();
    preprocess_work_completed_ = index_builder_.progress().work_completed;
    preprocess_work_total_ = index_builder_.progress().work_total;

    if (!index_builder_.running()) {
        index_ = index_builder_.takeIndex();
        status_ = index_.status();
        if (status_ == BaselineStatus::Completed) {
            port_scratch_.resetForGraph(index_.ports().numPorts());
        }
        preprocess_active_ = false;
        return true;
    }

    return finished;
}

void BrickSearchBaseline::cancelPreprocess() noexcept {
    resetPreprocessState();
    status_ = BaselineStatus::NotRun;
}

bool BrickSearchBaseline::preprocessActive() const noexcept {
    return preprocess_active_;
}

void BrickSearchBaseline::preprocess(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSize nominal_tile_size,
    const uint64_t max_memory_bytes
) {
    beginPreprocess(graph, layout, nominal_tile_size, max_memory_bytes);
    while (!preprocessStep()) {
    }
}

ReachabilityAnswer BrickSearchBaseline::query(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source >= index_.tiles().decomposition().mapWidth()
            * index_.tiles().decomposition().mapHeight()
        || target >= index_.tiles().decomposition().mapWidth()
            * index_.tiles().decomposition().mapHeight()) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source == target) {
        return ReachabilityAnswer::Reachable;
    }

    return queryPortBfsWithAttachments(index_, source, target, port_scratch_);
}

}  // namespace hbrick
