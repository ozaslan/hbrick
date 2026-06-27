#include "hbrick/graph/connected_components.hpp"

#include <algorithm>
#include <limits>
#include <vector>

#include "hbrick/graph/csr_graph.hpp"

namespace hbrick {

namespace {

class UnionFind {
public:
    explicit UnionFind(const uint32_t size)
        : parent_(size), component_size_(size, 1U) {
        for (uint32_t index = 0U; index < size; ++index) {
            parent_[index] = index;
        }
    }

    uint32_t find(uint32_t vertex) const noexcept {
        uint32_t root = vertex;
        while (parent_[root] != root) {
            root = parent_[root];
        }
        return root;
    }

    uint32_t findMutable(uint32_t vertex) noexcept {
        uint32_t root = vertex;
        while (parent_[root] != root) {
            root = parent_[root];
        }
        while (parent_[vertex] != vertex) {
            const uint32_t next = parent_[vertex];
            parent_[vertex] = root;
            vertex = next;
        }
        return root;
    }

    void unite(const uint32_t lhs, const uint32_t rhs) noexcept {
        uint32_t root_lhs = findMutable(lhs);
        uint32_t root_rhs = findMutable(rhs);
        if (root_lhs == root_rhs) {
            return;
        }
        if (component_size_[root_lhs] < component_size_[root_rhs]) {
            parent_[root_lhs] = root_rhs;
            component_size_[root_rhs] += component_size_[root_lhs];
        } else {
            parent_[root_rhs] = root_lhs;
            component_size_[root_lhs] += component_size_[root_rhs];
        }
    }

    [[nodiscard]] uint32_t largestComponentSize() const noexcept {
        uint32_t largest = 0U;
        for (uint32_t index = 0U; index < parent_.size(); ++index) {
            if (parent_[index] == index) {
                largest = std::max(largest, component_size_[index]);
            }
        }
        return largest;
    }

    [[nodiscard]] const std::vector<uint32_t>& parent() const noexcept {
        return parent_;
    }

private:
    std::vector<uint32_t> parent_;
    std::vector<uint32_t> component_size_;
};

void uniteAlongEdges(const CsrGraph& graph, UnionFind& union_find) noexcept {
    for (uint32_t from = 0U; from < graph.numVertices(); ++from) {
        for (const uint32_t to : graph.outNeighbors(from)) {
            union_find.unite(from, to);
        }
    }
}

void denseLabelRoots(
    const UnionFind& union_find,
    std::span<uint32_t> component_labels
) noexcept {
    const std::vector<uint32_t>& parent = union_find.parent();
    std::vector<uint32_t> root_to_label(parent.size(), std::numeric_limits<uint32_t>::max());
    uint32_t next_label = 0U;
    for (uint32_t vertex = 0U; vertex < parent.size(); ++vertex) {
        const uint32_t root = union_find.find(vertex);
        uint32_t& label = root_to_label[root];
        if (label == std::numeric_limits<uint32_t>::max()) {
            label = next_label++;
        }
        component_labels[vertex] = label;
    }
}

}  // namespace

uint32_t largestUndirectedComponentSize(const CsrGraph& graph) noexcept {
    const uint32_t num_vertices = graph.numVertices();
    if (num_vertices == 0U) {
        return 0U;
    }

    UnionFind union_find{num_vertices};
    uniteAlongEdges(graph, union_find);
    return union_find.largestComponentSize();
}

uint32_t fillUndirectedComponentLabels(
    const CsrGraph& graph,
    const std::span<uint32_t> component_labels
) noexcept {
    const uint32_t num_vertices = graph.numVertices();
    if (num_vertices == 0U || component_labels.size() < num_vertices) {
        return 0U;
    }

    UnionFind union_find{num_vertices};
    uniteAlongEdges(graph, union_find);
    denseLabelRoots(union_find, component_labels.subspan(0U, num_vertices));
    return union_find.largestComponentSize();
}

}  // namespace hbrick
