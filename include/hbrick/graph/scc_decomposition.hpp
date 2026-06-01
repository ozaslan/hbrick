#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

class SccDecomposition {
public:
    SccDecomposition() = default;

    [[nodiscard]] static SccDecomposition compute(
        const CsrGraph& graph,
        GraphSearchScratch& scratch
    );

    [[nodiscard]] uint32_t numVertices() const noexcept { return num_vertices_; }
    [[nodiscard]] uint32_t numComponents() const noexcept { return num_components_; }
    [[nodiscard]] uint32_t componentOf(uint32_t vertex) const noexcept;

private:
    SccDecomposition(uint32_t num_vertices, uint32_t num_components, std::vector<uint32_t> component_ids);

    uint32_t num_vertices_ = 0;
    uint32_t num_components_ = 0;
    std::vector<uint32_t> component_ids_;
};

}  // namespace hbrick
