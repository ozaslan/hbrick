#include "hbrick/graph/scc_decomposition.hpp"

#include <span>
#include <vector>

#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/edge32.hpp"

namespace hbrick {

namespace {

struct DfsFrame {
    uint32_t vertex = 0;
    uint32_t next_neighbor_index = 0;
};

[[nodiscard]] CsrGraph buildTransposeGraph(const CsrGraph& graph) {
    CsrGraphBuilder builder{graph.numVertices()};
    for (const Edge32& edge : graph.edges()) {
        builder.addEdge(edge.to, edge.from);
    }
    return builder.build();
}

void fillFinishOrder(
    const CsrGraph& graph,
    GraphSearchScratch& scratch,
    std::vector<uint32_t>& finish_order
) {
    const uint32_t num_vertices = graph.numVertices();
    const uint32_t mark = scratch.nextMark();
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<DfsFrame> frames;
    frames.reserve(num_vertices);

    for (uint32_t start = 0; start < num_vertices; ++start) {
        if (visited[start] == mark) {
            continue;
        }

        frames.clear();
        visited[start] = mark;
        frames.push_back(DfsFrame{start, 0U});

        while (!frames.empty()) {
            DfsFrame& frame = frames.back();
            const std::span<const uint32_t> neighbors = graph.outNeighbors(frame.vertex);

            if (frame.next_neighbor_index < neighbors.size()) {
                const uint32_t neighbor = neighbors[frame.next_neighbor_index];
                ++frame.next_neighbor_index;

                if (visited[neighbor] != mark) {
                    visited[neighbor] = mark;
                    frames.push_back(DfsFrame{neighbor, 0U});
                }
            } else {
                finish_order.push_back(frame.vertex);
                frames.pop_back();
            }
        }
    }
}

void assignComponentsFromTranspose(
    const CsrGraph& transpose_graph,
    const std::vector<uint32_t>& finish_order,
    GraphSearchScratch& scratch,
    std::vector<uint32_t>& component_ids,
    uint32_t& num_components
) {
    const uint32_t mark = scratch.nextMark();
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<uint32_t>& stack = scratch.stack();

    num_components = 0;
    for (std::size_t index = finish_order.size(); index > 0; --index) {
        const uint32_t start = finish_order[index - 1U];
        if (visited[start] == mark) {
            continue;
        }

        stack.clear();
        stack.push_back(start);
        visited[start] = mark;

        while (!stack.empty()) {
            const uint32_t vertex = stack.back();
            stack.pop_back();
            component_ids[vertex] = num_components;

            for (const uint32_t neighbor : transpose_graph.outNeighbors(vertex)) {
                if (visited[neighbor] == mark) {
                    continue;
                }
                visited[neighbor] = mark;
                stack.push_back(neighbor);
            }
        }

        ++num_components;
    }
}

}  // namespace

SccDecomposition::SccDecomposition(
    const uint32_t num_vertices,
    const uint32_t num_components,
    std::vector<uint32_t> component_ids
)
    : num_vertices_(num_vertices),
      num_components_(num_components),
      component_ids_(std::move(component_ids)) {}

SccDecomposition SccDecomposition::compute(
    const CsrGraph& graph,
    GraphSearchScratch& scratch
) {
    const uint32_t num_vertices = graph.numVertices();
    scratch.resetForGraph(num_vertices);

    std::vector<uint32_t> finish_order;
    finish_order.reserve(num_vertices);
    fillFinishOrder(graph, scratch, finish_order);

    const CsrGraph transpose_graph = buildTransposeGraph(graph);
    std::vector<uint32_t> component_ids(num_vertices, 0U);
    uint32_t num_components = 0;
    assignComponentsFromTranspose(
        transpose_graph,
        finish_order,
        scratch,
        component_ids,
        num_components
    );

    return SccDecomposition{num_vertices, num_components, std::move(component_ids)};
}

uint32_t SccDecomposition::componentOf(const uint32_t vertex) const noexcept {
    if (vertex >= num_vertices_) {
        return 0U;
    }
    return component_ids_[vertex];
}

}  // namespace hbrick
