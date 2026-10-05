#include "hbrick/baselines/baseline_graph_utils.hpp"

#include <algorithm>
#include <cassert>

#include "hbrick/graph/csr_graph_builder.hpp"

namespace hbrick {

CsrGraph buildTransposeGraph(const CsrGraph& graph) {
    return graph.transpose();
}

void collectForwardReachable(
    const CsrGraph& graph,
    const uint32_t source,
    GraphSearchScratch& scratch,
    std::vector<uint32_t>& reachable_vertices
) {
    (void)collectForwardReachableTimed(
        graph,
        source,
        scratch,
        reachable_vertices,
        []() noexcept -> bool { return false; }
    );
}

void sortUniqueLabelsInPlace(std::vector<uint32_t>& labels) {
    std::sort(labels.begin(), labels.end());
    const auto duplicate_begin = std::unique(labels.begin(), labels.end());
    labels.erase(duplicate_begin, labels.end());
}

}  // namespace hbrick
