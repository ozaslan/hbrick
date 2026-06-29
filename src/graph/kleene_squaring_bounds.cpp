#include "hbrick/graph/kleene_squaring_bounds.hpp"

#include <algorithm>
#include <vector>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/bit/bit_vector.hpp"
#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/connected_components.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/scc_decomposition.hpp"

namespace hbrick {

namespace {

[[nodiscard]] uint32_t largestStronglyConnectedComponentSize(
    const SccDecomposition& decomposition
) noexcept {
    const uint32_t num_vertices = decomposition.numVertices();
    const uint32_t num_components = decomposition.numComponents();
    if (num_vertices == 0U || num_components == 0U) {
        return 0U;
    }

    std::vector<uint32_t> component_sizes(num_components, 0U);
    for (uint32_t vertex = 0U; vertex < num_vertices; ++vertex) {
        const uint32_t component = decomposition.componentOf(vertex);
        ++component_sizes[component];
    }

    uint32_t largest = 0U;
    for (const uint32_t size : component_sizes) {
        largest = std::max(largest, size);
    }
    return largest;
}

[[nodiscard]] KleeneSquaringBoundStats boundStatsFromStructure(
    const uint32_t largest_undirected_component,
    const uint32_t num_strongly_connected_components,
    const uint32_t largest_strongly_connected_component
) noexcept {
    KleeneSquaringBoundStats stats{};
    stats.largest_undirected_component = largest_undirected_component;
    stats.num_strongly_connected_components = num_strongly_connected_components;
    stats.largest_strongly_connected_component = largest_strongly_connected_component;

    const uint32_t undirected_bound =
        BooleanClosure::kleeneSquaringCountForLargestComponent(largest_undirected_component);
    const uint32_t structural_scale =
        num_strongly_connected_components + largest_strongly_connected_component;
    const uint32_t structural_bound =
        BooleanClosure::kleeneSquaringCountForLargestComponent(structural_scale);
    stats.squaring_count = std::min(undirected_bound, structural_bound);
    return stats;
}

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

}  // namespace

KleeneSquaringBoundStats kleeneSquaringBoundStatsForCsrGraph(
    const CsrGraph& graph,
    GraphSearchScratch& scratch
) noexcept {
    const uint32_t largest_undirected_component = largestUndirectedComponentSize(graph);
    const SccDecomposition decomposition = SccDecomposition::compute(graph, scratch);
    return boundStatsFromStructure(
        largest_undirected_component,
        decomposition.numComponents(),
        largestStronglyConnectedComponentSize(decomposition)
    );
}

KleeneSquaringBoundStats kleeneSquaringBoundStatsForReflexiveAdjacency(
    const BitMatrix& reflexive_adjacency,
    GraphSearchScratch& scratch
) noexcept {
    const uint32_t largest_undirected_component =
        largestUndirectedComponentSizeFromReflexiveAdjacency(reflexive_adjacency);
    const CsrGraph graph = buildDirectedCsrFromReflexiveAdjacency(reflexive_adjacency);
    const SccDecomposition decomposition = SccDecomposition::compute(graph, scratch);
    return boundStatsFromStructure(
        largest_undirected_component,
        decomposition.numComponents(),
        largestStronglyConnectedComponentSize(decomposition)
    );
}

uint32_t kleeneSquaringCountForCsrGraph(
    const CsrGraph& graph,
    GraphSearchScratch& scratch
) noexcept {
    return kleeneSquaringBoundStatsForCsrGraph(graph, scratch).squaring_count;
}

uint32_t kleeneSquaringCountForReflexiveAdjacency(
    const BitMatrix& reflexive_adjacency,
    GraphSearchScratch& scratch
) noexcept {
    return kleeneSquaringBoundStatsForReflexiveAdjacency(reflexive_adjacency, scratch)
        .squaring_count;
}

}  // namespace hbrick
