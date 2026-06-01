#include "hbrick/graph/graph_search_scratch.hpp"

#include <algorithm>
#include <limits>

namespace hbrick {

GraphSearchScratch::GraphSearchScratch(const uint32_t num_vertices) {
    resetForGraph(num_vertices);
}

void GraphSearchScratch::resetForGraph(const uint32_t num_vertices) {
    visited_mark_.assign(num_vertices, 0U);
    queue_.clear();
    stack_.clear();
    queue_.reserve(num_vertices);
    stack_.reserve(num_vertices);
    current_mark_ = 1U;
}

uint32_t GraphSearchScratch::nextMark() {
    if (current_mark_ == std::numeric_limits<uint32_t>::max()) {
        std::fill(visited_mark_.begin(), visited_mark_.end(), 0U);
        current_mark_ = 1U;
        return 1U;
    }

    return current_mark_++;
}

size_t GraphSearchScratch::memoryBytes() const noexcept {
    return (queue_.capacity() + stack_.capacity() + visited_mark_.capacity()) *
           sizeof(uint32_t);
}

}  // namespace hbrick
