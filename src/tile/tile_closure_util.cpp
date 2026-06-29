#include "hbrick/tile/tile_closure_util.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <vector>

#include "hbrick/bit/bit_vector.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/tile/hbrick_config.hpp"

namespace hbrick {

namespace {

struct GlobalLocalEntry {
    uint32_t global_vertex = 0U;
    uint32_t local_index = 0U;
};

[[nodiscard]] uint32_t lookupLocalIndex(
    const std::vector<GlobalLocalEntry>& sorted_map,
    const uint32_t global_vertex
) noexcept {
    const auto it = std::lower_bound(
        sorted_map.begin(),
        sorted_map.end(),
        global_vertex,
        [](const GlobalLocalEntry& entry, const uint32_t global) noexcept {
            return entry.global_vertex < global;
        }
    );
    if (it == sorted_map.end() || it->global_vertex != global_vertex) {
        return std::numeric_limits<uint32_t>::max();
    }
    return it->local_index;
}

}  // namespace

bool isUnlimitedMemoryBudget(const uint64_t max_memory_bytes) noexcept {
    return max_memory_bytes >= kHBrickUnlimitedMemoryBytes;
}

uint64_t exactBitMatrixStorageBytes(
    const uint32_t num_rows,
    const uint32_t num_cols
) noexcept {
    if (num_rows == 0U || num_cols == 0U) {
        return 0U;
    }
    const uint64_t words_per_row = BitVector::wordCount(static_cast<size_t>(num_cols));
    return static_cast<uint64_t>(num_rows) * words_per_row * sizeof(uint64_t);
}

bool canAllocateTileReflexiveAdjacency(
    const uint32_t num_vertices,
    const uint64_t max_memory_bytes
) noexcept {
    if (isUnlimitedMemoryBudget(max_memory_bytes)) {
        return true;
    }
    return exactBitMatrixStorageBytes(num_vertices, num_vertices) <= max_memory_bytes;
}

BitMatrix buildTileReflexiveAdjacencyOrThrow(
    const CsrGraph& graph,
    const uint64_t max_memory_bytes
) {
    const uint32_t num_vertices = graph.numVertices();
    if (!canAllocateTileReflexiveAdjacency(num_vertices, max_memory_bytes)) {
        throw std::runtime_error(
            "Reflexive adjacency matrix exceeds configured memory limit"
        );
    }

    BitMatrix relation(num_vertices, num_vertices);

    for (uint32_t vertex = 0; vertex < num_vertices; ++vertex) {
        relation.set(vertex, vertex);
        for (const uint32_t neighbor : graph.outNeighbors(vertex)) {
            relation.set(vertex, neighbor);
        }
    }

    return relation;
}

uint32_t largestUndirectedComponentSizeFromAdjacency(
    const BitMatrix& adjacency
) noexcept {
    const uint32_t num_vertices = adjacency.numRows();
    if (num_vertices == 0U || adjacency.numCols() != num_vertices) {
        return 0U;
    }

    std::vector<uint32_t> parent(num_vertices);
    std::vector<uint32_t> component_size(num_vertices, 1U);
    for (uint32_t vertex = 0U; vertex < num_vertices; ++vertex) {
        parent[vertex] = vertex;
    }

    const auto find_root = [&](uint32_t vertex) {
        uint32_t root = vertex;
        while (parent[root] != root) {
            root = parent[root];
        }
        while (parent[vertex] != vertex) {
            const uint32_t next = parent[vertex];
            parent[vertex] = root;
            vertex = next;
        }
        return root;
    };

    const auto unite = [&](uint32_t lhs, uint32_t rhs) {
        uint32_t root_lhs = find_root(lhs);
        uint32_t root_rhs = find_root(rhs);
        if (root_lhs == root_rhs) {
            return;
        }
        if (component_size[root_lhs] < component_size[root_rhs]) {
            parent[root_lhs] = root_rhs;
            component_size[root_rhs] += component_size[root_lhs];
        } else {
            parent[root_rhs] = root_lhs;
            component_size[root_lhs] += component_size[root_rhs];
        }
    };

    const size_t num_words = adjacency.row(0U).numWords();
    const size_t full_words = static_cast<size_t>(num_vertices) / 64U;
    const uint32_t tail_bits = num_vertices % 64U;
    const uint64_t tail_mask =
        tail_bits == 0U ? ~0ULL : ((1ULL << tail_bits) - 1ULL);

    for (uint32_t from = 0U; from < num_vertices; ++from) {
        const BitVector& row = adjacency.row(from);
        for (size_t word_index = 0U; word_index < num_words; ++word_index) {
            uint64_t bits = row.word(word_index);
            if (word_index == full_words && tail_bits != 0U) {
                bits &= tail_mask;
            }
            while (bits != 0U) {
                const uint32_t to = static_cast<uint32_t>(
                    word_index * 64U + static_cast<size_t>(std::countr_zero(bits))
                );
                if (from != to) {
                    unite(from, to);
                }
                bits &= bits - 1U;
            }
        }
    }

    uint32_t largest = 0U;
    for (uint32_t vertex = 0U; vertex < num_vertices; ++vertex) {
        if (parent[vertex] == vertex) {
            largest = std::max(largest, component_size[vertex]);
        }
    }
    return largest;
}

CsrGraph buildInducedSubgraph(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    const TileSlot& bbox,
    const std::span<const uint32_t> global_vertices
) {
    const uint32_t local_count = static_cast<uint32_t>(global_vertices.size());
    CsrGraphBuilder builder{local_count};
    if (local_count == 0U) {
        return builder.build();
    }

    std::vector<GlobalLocalEntry> global_to_local;
    global_to_local.reserve(local_count);
    for (uint32_t local_index = 0U; local_index < local_count; ++local_index) {
        global_to_local.push_back(GlobalLocalEntry{
            global_vertices[local_index],
            local_index
        });
    }
    std::sort(
        global_to_local.begin(),
        global_to_local.end(),
        [](const GlobalLocalEntry& lhs, const GlobalLocalEntry& rhs) noexcept {
            return lhs.global_vertex < rhs.global_vertex;
        }
    );

    for (uint32_t local_from = 0U; local_from < local_count; ++local_from) {
        const uint32_t global_from = global_vertices[local_from];
        for (const uint32_t global_to : graph.outNeighbors(global_from)) {
            const uint32_t local_to = lookupLocalIndex(global_to_local, global_to);
            if (local_to == std::numeric_limits<uint32_t>::max()) {
                continue;
            }

            const GridCoord to_coord = graph.coordFromVertex(global_to);
            if (!bbox.contains(to_coord) || !layout.isPassable(to_coord)) {
                continue;
            }

            builder.addEdge(local_from, local_to);
        }
    }

    return builder.build();
}

}  // namespace hbrick
