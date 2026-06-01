#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/edge32.hpp"

namespace hbrick {

class CsrGraphBuilder {
public:
    explicit CsrGraphBuilder(uint32_t num_vertices);

    [[nodiscard]] uint32_t numVertices() const noexcept { return num_vertices_; }
    [[nodiscard]] uint64_t pendingEdgeCount() const noexcept { return edges_.size(); }

    void addEdge(uint32_t from, uint32_t to);
    void addEdge(Edge32 edge);

    void clear() noexcept;

    [[nodiscard]] CsrGraph build() const;

private:
    void validateEndpoint(uint32_t vertex) const;

    uint32_t num_vertices_ = 0;
    std::vector<Edge32> edges_;
};

}  // namespace hbrick
