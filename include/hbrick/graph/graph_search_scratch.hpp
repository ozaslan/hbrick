#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hbrick {

class GraphSearchScratchOverflowTest;

class GraphSearchScratch {
public:
    GraphSearchScratch() = default;
    explicit GraphSearchScratch(uint32_t num_vertices);

    void resetForGraph(uint32_t num_vertices);

    uint32_t nextMark();

    std::vector<uint32_t>& queue() noexcept { return queue_; }
    std::vector<uint32_t>& stack() noexcept { return stack_; }
    std::vector<uint32_t>& visitedMark() noexcept { return visited_mark_; }

    const std::vector<uint32_t>& queue() const noexcept { return queue_; }
    const std::vector<uint32_t>& stack() const noexcept { return stack_; }
    const std::vector<uint32_t>& visitedMark() const noexcept { return visited_mark_; }

    [[nodiscard]] size_t memoryBytes() const noexcept;

private:
    friend class GraphSearchScratchOverflowTest;

    std::vector<uint32_t> queue_;
    std::vector<uint32_t> stack_;
    std::vector<uint32_t> visited_mark_;

    uint32_t current_mark_ = 1;
};

}  // namespace hbrick
