#include "hbrick/graph/condensation_graph.hpp"

#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/edge32.hpp"

namespace hbrick {

CondensationGraph::CondensationGraph(SccDecomposition decomposition, CsrGraph dag)
    : decomposition_(std::move(decomposition)), dag_(std::move(dag)) {}

CondensationGraph CondensationGraph::fromGraph(
    const CsrGraph& graph,
    const SccDecomposition& decomposition
) {
    const uint32_t num_components = decomposition.numComponents();
    CsrGraphBuilder builder{num_components};

    for (const Edge32& edge : graph.edges()) {
        const uint32_t source_component = decomposition.componentOf(edge.from);
        const uint32_t target_component = decomposition.componentOf(edge.to);
        if (source_component != target_component) {
            builder.addEdge(source_component, target_component);
        }
    }

    return CondensationGraph{decomposition, builder.build()};
}

}  // namespace hbrick
