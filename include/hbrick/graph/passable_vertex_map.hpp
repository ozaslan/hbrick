/**
 * @file passable_vertex_map.hpp
 * @ingroup hbrick_graph
 * @brief Grid-id to compact passable-vertex mapping for generic baselines.
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/core/vertex_id.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/grid/maze_layout.hpp"

namespace hbrick {

/**
 * @brief Bidirectional map between row-major grid vertex ids and compact ids.
 * @ingroup hbrick_graph
 *
 * Compact ids run @c 0 .. N-1 over passable cells only. Blocked grid slots map
 * to @ref kInvalidVertexId. Spatial indexes keep grid ids; generic CSR
 * baselines use compact ids so blocked cells are not charged as vertices.
 */
class PassableVertexMap {
public:
    /** @brief Builds the map from @p layout passability. @ingroup hbrick_graph */
    [[nodiscard]] static PassableVertexMap fromLayout(const MazeLayout& layout);

    /** @brief Number of grid slots (@c width * height). @ingroup hbrick_graph */
    [[nodiscard]] uint32_t gridVertexCount() const noexcept {
        return static_cast<uint32_t>(grid_to_compact_.size());
    }

    /** @brief Number of passable vertices (@c N). @ingroup hbrick_graph */
    [[nodiscard]] uint32_t compactVertexCount() const noexcept {
        return static_cast<uint32_t>(compact_to_grid_.size());
    }

    /**
     * @brief Compact id of grid vertex @p grid_vertex, or @ref kInvalidVertexId.
     * @ingroup hbrick_graph
     */
    [[nodiscard]] uint32_t toCompact(uint32_t grid_vertex) const noexcept;

    /**
     * @brief Grid id of compact vertex @p compact_vertex, or @ref kInvalidVertexId.
     * @ingroup hbrick_graph
     */
    [[nodiscard]] uint32_t toGrid(uint32_t compact_vertex) const noexcept;

    /** @brief Passable grid vertex ids in row-major order. @ingroup hbrick_graph */
    [[nodiscard]] std::span<const uint32_t> passableGridVertices() const noexcept {
        return compact_to_grid_;
    }

private:
    std::vector<uint32_t> grid_to_compact_{};
    std::vector<uint32_t> compact_to_grid_{};
};

/**
 * @brief Induced CSR on passable cells of @p grid_graph using compact ids.
 * @ingroup hbrick_graph
 *
 * Vertex count equals @p map.compactVertexCount(). Edges whose endpoints are
 * not both passable are dropped. Isolated passable cells remain as vertices.
 */
[[nodiscard]] CsrGraph inducePassableCsr(
    const DirectedGridGraph& grid_graph,
    const PassableVertexMap& map
);

/**
 * @brief Collects passable row-major grid vertex ids from @p layout.
 * @ingroup hbrick_graph
 */
[[nodiscard]] std::vector<uint32_t> collectPassableGridVertices(const MazeLayout& layout);

}  // namespace hbrick
