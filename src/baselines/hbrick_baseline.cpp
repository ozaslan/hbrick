#include "hbrick/baselines/hbrick_baseline.hpp"

#include <limits>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/port_index.hpp"
#include "hbrick/tile/region_node.hpp"
#include "hbrick/tile/super_tile_summary.hpp"

namespace hbrick {

namespace {

[[nodiscard]] bool vectorHasAnySet(const BitVector& vector) noexcept {
    for (size_t word_index = 0U; word_index < vector.numWords(); ++word_index) {
        if (vector.word(word_index) != 0U) {
            return true;
        }
    }
    return false;
}

void embedChildPorts(
    const BitVector& child,
    const BitMatrix& embedding,
    BitVector& out_gamma
) noexcept {
    out_gamma.clear();
    const uint32_t gamma_size = embedding.numRows();
    if (gamma_size == 0U) {
        return;
    }

    for (uint32_t child_port = 0U; child_port < embedding.numCols(); ++child_port) {
        if (!child.test(child_port)) {
            continue;
        }
        for (uint32_t gamma_index = 0U; gamma_index < gamma_size; ++gamma_index) {
            if (embedding.test(gamma_index, child_port)) {
                out_gamma.set(gamma_index);
            }
        }
    }
}

void multiplyVectorClosure(
    const BitVector& input,
    const BitMatrix& closure,
    BitVector& output
) noexcept {
    output.clear();
    for (uint32_t row = 0U; row < closure.numRows(); ++row) {
        if (!input.test(row)) {
            continue;
        }
        output.rowOr(closure.row(row));
    }
}

void projectToExterior(
    const BitVector& gamma,
    const std::span<const uint32_t> exterior_gamma_indices,
    BitVector& exterior
) noexcept {
    exterior.clear();
    for (uint32_t exterior_index = 0U; exterior_index < exterior_gamma_indices.size();
         ++exterior_index) {
        if (gamma.test(exterior_gamma_indices[exterior_index])) {
            exterior.set(exterior_index);
        }
    }
}

void liftExteriorToGamma(
    const BitVector& exterior,
    const std::span<const uint32_t> exterior_gamma_indices,
    BitVector& gamma
) noexcept {
    gamma.clear();
    for (uint32_t exterior_index = 0U; exterior_index < exterior_gamma_indices.size();
         ++exterior_index) {
        if (exterior.test(exterior_index)) {
            gamma.set(exterior_gamma_indices[exterior_index]);
        }
    }
}

[[nodiscard]] bool bilinearMeet(
    const BitVector& left_gamma,
    const BitMatrix& closure,
    const BitVector& right_gamma
) noexcept {
    for (uint32_t row = 0U; row < closure.numRows(); ++row) {
        if (!left_gamma.test(row)) {
            continue;
        }
        for (uint32_t col = 0U; col < closure.numCols(); ++col) {
            if (closure.test(row, col) && right_gamma.test(col)) {
                return true;
            }
        }
    }
    return false;
}

void buildAncestorChain(
    const HierarchyTree& hierarchy,
    const RegionNodeId leaf,
    std::vector<RegionNodeId>& chain
) noexcept {
    chain.clear();
    RegionNodeId current = leaf;
    while (true) {
        chain.push_back(current);
        const RegionNode& node = hierarchy.node(current.level, current.index);
        if (!node.has_parent) {
            break;
        }
        current = node.parent;
    }
}

[[nodiscard]] uint32_t findChildEmbeddingIndex(
    const RegionNode& parent,
    const RegionNodeId child_id
) noexcept {
    for (uint32_t child_index = 0U; child_index < parent.children.size(); ++child_index) {
        if (parent.children[child_index] == child_id) {
            return child_index;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

void propagateSourceExteriorUp(
    const HBrickIndex& index,
    const std::span<const RegionNodeId> source_chain,
    HBrickQueryScratch& scratch
) noexcept {
    const HierarchyTree& hierarchy = index.hierarchy();

    for (uint32_t chain_index = 0U; chain_index + 1U < source_chain.size(); ++chain_index) {
        const RegionNodeId child_id = source_chain[chain_index];
        const RegionNodeId parent_id = source_chain[chain_index + 1U];
        const RegionNode& parent_node = hierarchy.node(parent_id.level, parent_id.index);
        const SuperTileSummary& parent_summary =
            index.superSummary(parent_id.level, parent_id.index);
        const uint32_t child_embedding_index =
            findChildEmbeddingIndex(parent_node, child_id);
        if (child_embedding_index == std::numeric_limits<uint32_t>::max()) {
            return;
        }

        const BitMatrix& embedding =
            parent_summary.child_embeddings[child_embedding_index];
        BitVector& child_vector = scratch.sourceChain()[chain_index];
        BitVector& parent_vector = scratch.sourceChain()[chain_index + 1U];

        if (child_id.level == 0U) {
            embedChildPorts(child_vector, embedding, scratch.gammaA());
        } else {
            const SuperTileSummary& child_summary =
                index.superSummary(child_id.level, child_id.index);
            liftExteriorToGamma(
                child_vector,
                child_summary.exterior_gamma_indices,
                scratch.gammaC()
            );
            embedChildPorts(scratch.gammaC(), embedding, scratch.gammaA());
        }

        multiplyVectorClosure(
            scratch.gammaA(),
            parent_summary.interface_closure,
            scratch.gammaB()
        );
        projectToExterior(
            scratch.gammaB(),
            parent_summary.exterior_gamma_indices,
            parent_vector
        );
    }
}

void propagateTargetExteriorUp(
    const HBrickIndex& index,
    const std::span<const RegionNodeId> target_chain,
    HBrickQueryScratch& scratch
) noexcept {
    const HierarchyTree& hierarchy = index.hierarchy();

    for (uint32_t chain_index = 0U; chain_index + 1U < target_chain.size(); ++chain_index) {
        const RegionNodeId child_id = target_chain[chain_index];
        const RegionNodeId parent_id = target_chain[chain_index + 1U];
        const RegionNode& parent_node = hierarchy.node(parent_id.level, parent_id.index);
        const SuperTileSummary& parent_summary =
            index.superSummary(parent_id.level, parent_id.index);
        const uint32_t child_embedding_index =
            findChildEmbeddingIndex(parent_node, child_id);
        if (child_embedding_index == std::numeric_limits<uint32_t>::max()) {
            return;
        }

        const BitMatrix& embedding =
            parent_summary.child_embeddings[child_embedding_index];
        BitVector& child_vector = scratch.targetChain()[chain_index];
        BitVector& parent_vector = scratch.targetChain()[chain_index + 1U];

        if (child_id.level == 0U) {
            embedChildPorts(child_vector, embedding, scratch.gammaA());
        } else {
            const SuperTileSummary& child_summary =
                index.superSummary(child_id.level, child_id.index);
            liftExteriorToGamma(
                child_vector,
                child_summary.exterior_gamma_indices,
                scratch.gammaC()
            );
            embedChildPorts(scratch.gammaC(), embedding, scratch.gammaA());
        }

        multiplyVectorClosure(
            scratch.gammaA(),
            parent_summary.interface_closure,
            scratch.gammaB()
        );
        projectToExterior(
            scratch.gammaB(),
            parent_summary.exterior_gamma_indices,
            parent_vector
        );
    }
}

[[nodiscard]] ReachabilityAnswer queryFlatBrickPortBfsImpl(
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

    if (scratch.visitedMark().size() < num_port_vertices) {
        scratch.resetForGraph(num_port_vertices);
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

ReachabilityAnswer HBrickBaseline::queryFlatBrickPortBfs(
    const BrickIndex& index,
    const uint32_t source,
    const uint32_t target,
    GraphSearchScratch& scratch
) noexcept {
    return queryFlatBrickPortBfsImpl(index, source, target, scratch);
}

ReachabilityAnswer HBrickBaseline::queryHierarchical(
    const HBrickIndex& index,
    const uint32_t source,
    const uint32_t target,
    HBrickQueryScratch& scratch
) noexcept {
    const BrickTileIndex& tiles = index.brickIndex().tiles();
    const HierarchyTree& hierarchy = index.hierarchy();

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

    scratch.clearVectors();

    BitVector& source_leaf = scratch.sourceChain()[0U];
    for (uint32_t port_index = 0U; port_index < source_summary.numPorts(); ++port_index) {
        if (source_summary.vertex_to_boundary.test(source_local, port_index)) {
            source_leaf.set(port_index);
        }
    }
    if (!vectorHasAnySet(source_leaf)) {
        return ReachabilityAnswer::Unreachable;
    }

    BitVector& target_leaf = scratch.targetChain()[0U];
    for (uint32_t port_index = 0U; port_index < target_summary.numPorts(); ++port_index) {
        if (target_summary.boundary_to_vertex.test(port_index, target_local)) {
            target_leaf.set(port_index);
        }
    }
    if (!vectorHasAnySet(target_leaf)) {
        return ReachabilityAnswer::Unreachable;
    }

    std::vector<RegionNodeId> source_chain;
    std::vector<RegionNodeId> target_chain;
    source_chain.reserve(hierarchy.numLevels());
    target_chain.reserve(hierarchy.numLevels());
    buildAncestorChain(hierarchy, RegionNodeId{0U, source_tile}, source_chain);
    buildAncestorChain(hierarchy, RegionNodeId{0U, target_tile}, target_chain);

    propagateSourceExteriorUp(index, source_chain, scratch);
    propagateTargetExteriorUp(index, target_chain, scratch);

    for (uint32_t level = 1U; level < hierarchy.numLevels(); ++level) {
        if (level >= source_chain.size() || level >= target_chain.size()) {
            break;
        }
        if (source_chain[level] != target_chain[level]) {
            continue;
        }

        const RegionNode& parent_node = hierarchy.node(level, source_chain[level].index);
        const SuperTileSummary& parent_summary =
            index.superSummary(level, source_chain[level].index);
        const RegionNodeId child_source = source_chain[level - 1U];
        const RegionNodeId child_target = target_chain[level - 1U];

        const uint32_t child_source_index =
            findChildEmbeddingIndex(parent_node, child_source);
        const uint32_t child_target_index =
            findChildEmbeddingIndex(parent_node, child_target);
        if (child_source_index == std::numeric_limits<uint32_t>::max()
            || child_target_index == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        const BitMatrix& embedding_source =
            parent_summary.child_embeddings[child_source_index];
        const BitMatrix& embedding_target =
            parent_summary.child_embeddings[child_target_index];

        if (child_source.level == 0U) {
            embedChildPorts(
                scratch.sourceChain()[level - 1U],
                embedding_source,
                scratch.gammaA()
            );
        } else {
            const SuperTileSummary& child_summary =
                index.superSummary(child_source.level, child_source.index);
            liftExteriorToGamma(
                scratch.sourceChain()[level - 1U],
                child_summary.exterior_gamma_indices,
                scratch.gammaC()
            );
            embedChildPorts(scratch.gammaC(), embedding_source, scratch.gammaA());
        }

        if (child_target.level == 0U) {
            embedChildPorts(
                scratch.targetChain()[level - 1U],
                embedding_target,
                scratch.gammaB()
            );
        } else {
            const SuperTileSummary& child_summary =
                index.superSummary(child_target.level, child_target.index);
            liftExteriorToGamma(
                scratch.targetChain()[level - 1U],
                child_summary.exterior_gamma_indices,
                scratch.gammaC()
            );
            embedChildPorts(scratch.gammaC(), embedding_target, scratch.gammaB());
        }

        if (bilinearMeet(
                scratch.gammaA(),
                parent_summary.interface_closure,
                scratch.gammaB()
            )) {
            return ReachabilityAnswer::Reachable;
        }
    }

    return ReachabilityAnswer::Unreachable;
}

void HBrickBaseline::preprocess(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const HBrickConfig config
) {
    status_ = BaselineStatus::NotRun;
    index_ = HBrickIndex{};
    scratch_ = HBrickQueryScratch{};

    index_ = HBrickIndex::build(graph, layout, config);
    status_ = index_.status();
    if (status_ == BaselineStatus::Completed) {
        scratch_.prepare(index_);
    }
}

uint64_t HBrickBaseline::indexStorageBytes() const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return 0U;
    }

    uint64_t bytes = 0U;
    for (uint32_t level = 1U; level < index_.hierarchy().numLevels(); ++level) {
        for (const SuperTileSummary& summary : index_.superLevel(level)) {
            bytes += summary.interface_closure.memoryBytes();
            bytes += summary.boundary_summary.memoryBytes();
            for (const BitMatrix& embedding : summary.child_embeddings) {
                bytes += embedding.memoryBytes();
            }
        }
    }

    for (const BaseTileSummary& summary : index_.brickIndex().tiles().summaries()) {
        bytes += summary.local_closure.memoryBytes();
        bytes += summary.boundary_summary.memoryBytes();
    }

    return bytes;
}

ReachabilityAnswer HBrickBaseline::query(
    const uint32_t source,
    const uint32_t target,
    HBrickQueryScratch& scratch,
    GraphSearchScratch& port_bfs_scratch
) const noexcept {
    if (status_ != BaselineStatus::Completed) {
        return ReachabilityAnswer::Unreachable;
    }

    const uint32_t num_vertices =
        index_.brickIndex().tiles().decomposition().mapWidth()
        * index_.brickIndex().tiles().decomposition().mapHeight();
    if (source >= num_vertices || target >= num_vertices) {
        return ReachabilityAnswer::Unreachable;
    }

    if (source == target) {
        return ReachabilityAnswer::Reachable;
    }

    if (!index_.hasSuperLevel(1U)) {
        return queryFlatBrickPortBfs(
            index_.brickIndex(),
            source,
            target,
            port_bfs_scratch
        );
    }

    return queryHierarchical(index_, source, target, scratch);
}

}  // namespace hbrick
