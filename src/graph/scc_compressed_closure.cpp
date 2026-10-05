#include "hbrick/graph/scc_compressed_closure.hpp"

#include <bit>
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
        return std::move(builder).build();
    }

    for (uint32_t from = 0U; from < num_vertices; ++from) {
        const BitVector& row = reflexive_adjacency.row(from);
        const uint64_t* words = row.wordsData();
        const size_t num_words = row.numWords();
        for (size_t w = 0U; w < num_words; ++w) {
            uint64_t bits = words[w];
            while (bits != 0U) {
                const unsigned bit = static_cast<unsigned>(std::countr_zero(bits));
                const uint32_t to = static_cast<uint32_t>(w * 64U + bit);
                if (from != to && to < num_vertices) {
                    builder.addEdge(from, to);
                }
                bits &= bits - 1U;
            }
        }
    }
    return std::move(builder).build();
}

void runDirectVertexKleeneClosure(
    BitMatrix& reflexive_vertex_adjacency,
    const CsrGraph& graph,
    GraphSearchScratch& scratch,
    BitMatrix* multiply_scratch,
    const KleeneSquaringOptions options
) {
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
    if (num_vertices <= 1U || num_components >= num_vertices) {
        return false;
    }
    if (num_vertices > 16U) {
        return (static_cast<double>(num_components) / static_cast<double>(num_vertices)) <= 0.85;
    }
    return true;
}

void computeDagTransitiveClosure(
    const CsrGraph& dag,
    BitMatrix& closure_in_out
) {
    const uint32_t num_components = dag.numVertices();
    if (num_components == 0U) {
        return;
    }

    // Check if the DAG is in natural topological order (u < v for all directed edges)
    bool is_natural_topo = true;
    for (uint32_t u = 0U; u < num_components; ++u) {
        for (const uint32_t v : dag.outNeighbors(u)) {
            if (u >= v) {
                is_natural_topo = false;
                break;
            }
        }
        if (!is_natural_topo) {
            break;
        }
    }

    if (is_natural_topo) {
        // In reverse topological order, each successor's transitive reachability is already finalized
        for (uint32_t idx = num_components; idx > 0U; --idx) {
            const uint32_t u = idx - 1U;
            BitVector& u_row = closure_in_out.row(u);
            for (const uint32_t v : dag.outNeighbors(u)) {
                if (v < num_components) {
                    u_row.rowOr(closure_in_out.row(v));
                }
            }
        }
    } else {
        // Fallback: Kahn's topological sort
        std::vector<uint32_t> in_degree(num_components, 0U);
        for (uint32_t u = 0U; u < num_components; ++u) {
            for (const uint32_t v : dag.outNeighbors(u)) {
                if (v < num_components) {
                    ++in_degree[v];
                }
            }
        }
        std::vector<uint32_t> topo_order;
        topo_order.reserve(num_components);
        for (uint32_t u = 0U; u < num_components; ++u) {
            if (in_degree[u] == 0U) {
                topo_order.push_back(u);
            }
        }
        size_t head = 0U;
        while (head < topo_order.size()) {
            const uint32_t u = topo_order[head++];
            for (const uint32_t v : dag.outNeighbors(u)) {
                if (v < num_components && --in_degree[v] == 0U) {
                    topo_order.push_back(v);
                }
            }
        }
        for (size_t i = topo_order.size(); i > 0U; --i) {
            const uint32_t u = topo_order[i - 1U];
            BitVector& u_row = closure_in_out.row(u);
            for (const uint32_t v : dag.outNeighbors(u)) {
                if (v < num_components) {
                    u_row.rowOr(closure_in_out.row(v));
                }
            }
        }
    }
}

SccCompressedReflexiveAdjacency buildSccCompressedReflexiveAdjacency(
    const CsrGraph& graph,
    const SccDecomposition& decomposition
) {
    const CondensationGraph condensation =
        CondensationGraph::fromGraph(graph, decomposition);
    const uint32_t num_components = decomposition.numComponents();
    BitMatrix component_adjacency(num_components, num_components);
    const CsrGraph& dag = condensation.dag();
    for (uint32_t component = 0U; component < num_components; ++component) {
        component_adjacency.set(component, component);
        for (const uint32_t to : dag.outNeighbors(component)) {
            component_adjacency.set(component, to);
        }
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

    // Group vertices by component using flat offsets
    std::vector<uint32_t> comp_offsets(num_components + 1U, 0U);
    for (uint32_t v = 0U; v < num_vertices; ++v) {
        ++comp_offsets[decomposition.componentOf(v) + 1U];
    }
    for (uint32_t c = 0U; c < num_components; ++c) {
        comp_offsets[c + 1U] += comp_offsets[c];
    }
    std::vector<uint32_t> comp_vertices(num_vertices);
    std::vector<uint32_t> cursor = comp_offsets;
    for (uint32_t v = 0U; v < num_vertices; ++v) {
        comp_vertices[cursor[decomposition.componentOf(v)]++] = v;
    }

    // For each component, compute the full reachable vertex mask once
    BitMatrix comp_reachable_verts(num_components, num_vertices);
    for (uint32_t c = 0U; c < num_components; ++c) {
        BitVector& c_row = comp_reachable_verts.row(c);
        const BitVector& comp_reach = component_closure.row(c);
        const size_t num_words = comp_reach.numWords();
        for (size_t w = 0U; w < num_words; ++w) {
            uint64_t bits = comp_reach.word(w);
            while (bits != 0U) {
                const unsigned bit = static_cast<unsigned>(std::countr_zero(bits));
                const uint32_t target_comp = static_cast<uint32_t>(w * 64U + bit);
                bits &= bits - 1U;
                if (target_comp < num_components) {
                    const uint32_t start = comp_offsets[target_comp];
                    const uint32_t end = comp_offsets[target_comp + 1U];
                    for (uint32_t i = start; i < end; ++i) {
                        c_row.set(comp_vertices[i]);
                    }
                }
            }
        }
    }

    // Assign the precomputed component mask to each vertex
    for (uint32_t source_vertex = 0U; source_vertex < num_vertices; ++source_vertex) {
        const uint32_t c = decomposition.componentOf(source_vertex);
        vertex_closure_out.row(source_vertex) = comp_reachable_verts.row(c);
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
) {
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
    computeDagTransitiveClosure(compressed.condensation_dag, compressed.component_adjacency);
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
) {
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
