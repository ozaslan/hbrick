#include "hbrick/graph/scc_compressed_closure.hpp"

#include <vector>

#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/bit/bit_vector.hpp"
#include "hbrick/graph/condensation_graph.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/edge32.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/kleene_squaring_bounds.hpp"

namespace hbrick {

namespace {

[[nodiscard]] CsrGraph buildDirectedCsrFromReflexiveAdjacency(
    const BitMatrix& reflexive_adjacency
) {
    const uint32_t num_vertices = reflexive_adjacency.numRows();
    CsrGraphBuilder builder{num_vertices};
    if (num_vertices == 0U || reflexive_adjacency.numCols() != num_vertices) {
        return builder.build();
    }

    for (uint32_t from = 0U; from < num_vertices; ++from) {
        const BitVector& row = reflexive_adjacency.row(from);
        for (uint32_t to = 0U; to < num_vertices; ++to) {
            if (from != to && row.test(to)) {
                builder.addEdge(from, to);
            }
        }
    }
    return builder.build();
}

[[nodiscard]] std::vector<std::vector<uint32_t>> verticesByComponent(
    const SccDecomposition& decomposition
) {
    const uint32_t num_vertices = decomposition.numVertices();
    const uint32_t num_components = decomposition.numComponents();
    std::vector<std::vector<uint32_t>> vertices_by_component(num_components);
    for (uint32_t vertex = 0U; vertex < num_vertices; ++vertex) {
        vertices_by_component[decomposition.componentOf(vertex)].push_back(vertex);
    }
    return vertices_by_component;
}

void runDirectVertexKleeneClosure(
    BitMatrix& reflexive_vertex_adjacency,
    const CsrGraph& graph,
    GraphSearchScratch& scratch,
    BitMatrix* multiply_scratch,
    const KleeneSquaringOptions options
) noexcept {
    const uint32_t squaring_count = kleeneSquaringCountForCsrGraph(graph, scratch);
    BooleanClosure::transitiveClosureKleeneSquaringInPlace(
        reflexive_vertex_adjacency,
        squaring_count,
        multiply_scratch,
        options
    );
}

}  // namespace

bool shouldUseSccCompressedClosure(
    const uint32_t num_vertices,
    const uint32_t num_components
) noexcept {
    return num_vertices > 1U && num_components < num_vertices;
}

SccCompressedReflexiveAdjacency buildSccCompressedReflexiveAdjacency(
    const CsrGraph& graph,
    const SccDecomposition& decomposition
) {
    const CondensationGraph condensation =
        CondensationGraph::fromGraph(graph, decomposition);
    const uint32_t num_components = decomposition.numComponents();
    BitMatrix component_adjacency(num_components, num_components);
    for (uint32_t component = 0U; component < num_components; ++component) {
        component_adjacency.set(component, component);
    }
    for (const Edge32& edge : condensation.dag().edges()) {
        component_adjacency.set(edge.from, edge.to);
    }

    return SccCompressedReflexiveAdjacency{
        decomposition,
        condensation.dag(),
        std::move(component_adjacency)
    };
}

SccCompressedReflexiveAdjacency buildSccCompressedReflexiveAdjacency(
    const BitMatrix& reflexive_vertex_adjacency,
    const SccDecomposition& decomposition
) {
    const CsrGraph graph = buildDirectedCsrFromReflexiveAdjacency(reflexive_vertex_adjacency);
    return buildSccCompressedReflexiveAdjacency(graph, decomposition);
}

void expandComponentClosureToVertexClosure(
    const SccDecomposition& decomposition,
    const BitMatrix& component_closure,
    BitMatrix& vertex_closure_out
) {
    const uint32_t num_vertices = decomposition.numVertices();
    const uint32_t num_components = decomposition.numComponents();
    if (num_vertices == 0U) {
        vertex_closure_out = BitMatrix{};
        return;
    }

    if (vertex_closure_out.numRows() != num_vertices
        || vertex_closure_out.numCols() != num_vertices) {
        vertex_closure_out = BitMatrix(num_vertices, num_vertices);
    }

    const std::vector<std::vector<uint32_t>> vertices_by_component =
        verticesByComponent(decomposition);

    for (uint32_t source_vertex = 0U; source_vertex < num_vertices; ++source_vertex) {
        BitVector& source_row = vertex_closure_out.row(source_vertex);
        source_row.clear();
        const uint32_t source_component = decomposition.componentOf(source_vertex);
        for (uint32_t target_component = 0U; target_component < num_components;
             ++target_component) {
            if (source_component != target_component
                && !component_closure.test(source_component, target_component)) {
                continue;
            }
            for (const uint32_t target_vertex :
                 vertices_by_component[target_component]) {
                source_row.set(target_vertex);
            }
        }
    }
}

bool vertexReachableInComponentClosure(
    const SccDecomposition& decomposition,
    const BitMatrix& component_closure,
    const uint32_t source_vertex,
    const uint32_t target_vertex
) noexcept {
    if (source_vertex >= decomposition.numVertices()
        || target_vertex >= decomposition.numVertices()) {
        return false;
    }

    const uint32_t source_component = decomposition.componentOf(source_vertex);
    const uint32_t target_component = decomposition.componentOf(target_vertex);
    return source_component == target_component
        || component_closure.test(source_component, target_component);
}

bool transitiveClosureKleeneSccCompressedInPlace(
    BitMatrix& reflexive_vertex_adjacency,
    const CsrGraph& graph,
    GraphSearchScratch& scratch,
    BitMatrix* multiply_scratch,
    const KleeneSquaringOptions options
) noexcept {
    const uint32_t num_vertices = graph.numVertices();
    if (num_vertices <= 1U) {
        return false;
    }

    const SccDecomposition decomposition = SccDecomposition::compute(graph, scratch);
    const uint32_t num_components = decomposition.numComponents();
    if (!shouldUseSccCompressedClosure(num_vertices, num_components)) {
        runDirectVertexKleeneClosure(
            reflexive_vertex_adjacency,
            graph,
            scratch,
            multiply_scratch,
            options
        );
        return false;
    }

    SccCompressedReflexiveAdjacency compressed =
        buildSccCompressedReflexiveAdjacency(graph, decomposition);
    scratch.resetForGraph(num_components);
    const uint32_t squaring_count =
        kleeneSquaringCountForCsrGraph(compressed.condensation_dag, scratch);
    BooleanClosure::transitiveClosureKleeneSquaringInPlace(
        compressed.component_adjacency,
        squaring_count,
        multiply_scratch,
        options
    );
    expandComponentClosureToVertexClosure(
        compressed.decomposition,
        compressed.component_adjacency,
        reflexive_vertex_adjacency
    );
    return true;
}

bool transitiveClosureKleeneSccCompressedInPlace(
    BitMatrix& reflexive_vertex_adjacency,
    GraphSearchScratch& scratch,
    BitMatrix* multiply_scratch,
    const KleeneSquaringOptions options
) noexcept {
    const CsrGraph graph = buildDirectedCsrFromReflexiveAdjacency(reflexive_vertex_adjacency);
    return transitiveClosureKleeneSccCompressedInPlace(
        reflexive_vertex_adjacency,
        graph,
        scratch,
        multiply_scratch,
        options
    );
}

}  // namespace hbrick
