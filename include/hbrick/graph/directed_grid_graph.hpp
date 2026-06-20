/**
 * @file directed_grid_graph.hpp
 * @ingroup hbrick_graph
 * @brief Directed graph embedded on a rectangular grid with coordinate mapping.
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/edge32.hpp"

namespace hbrick {

class DirectedGridGraphBuilder;

/**
 * @brief Grid-embedded directed graph backed by an internal @ref hbrick::CsrGraph.
 * @ingroup hbrick_graph
 *
 * Vertices correspond to grid cells in row-major order. Provides both raw CSR
 * traversal and coordinate conversion helpers for visualization and maze tests.
 */
class DirectedGridGraph {
public:
    /** @brief Constructs an empty grid graph. @ingroup hbrick_graph */
    DirectedGridGraph() = default;

    /** @brief Returns the grid width in cells. @return The grid width in cells. @ingroup hbrick_graph */
    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    /** @brief Returns the grid height in cells. @return The grid height in cells. @ingroup hbrick_graph */
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    /** @brief Returns the number of vertices (equal to @c width * height). @return Total vertex count. @ingroup hbrick_graph */
    [[nodiscard]] uint32_t numVertices() const noexcept { return graph_.numVertices(); }
    /** @brief Returns the number of directed edges. @return The number of directed edges. @ingroup hbrick_graph */
    [[nodiscard]] uint64_t numEdges() const noexcept { return graph_.numEdges(); }

    /**
     * @brief Returns outgoing neighbors of @p vertex.
     * @ingroup hbrick_graph
     *
     * Delegates to the internal CSR graph; allocation-free on the hot path.
     *
     * @param vertex Zero-based vertex index.
     * @return Documented value.
     */
    [[nodiscard]] std::span<const uint32_t> outNeighbors(uint32_t vertex) const noexcept {
        return graph_.outNeighbors(vertex);
    }

    /**
     * @brief Maps a vertex index to grid coordinates.
     * @ingroup hbrick_graph
     *
     * @param vertex Zero-based vertex index.
     * @return Grid coordinate of the corresponding cell.
     */
    [[nodiscard]] GridCoord coordFromVertex(uint32_t vertex) const noexcept;

    /**
     * @brief Maps a grid coordinate to its vertex index.
     * @ingroup hbrick_graph
     *
     * @param coord Cell coordinate.
     * @return Row-major linear vertex index.
     */
    [[nodiscard]] uint32_t vertexFromCoord(GridCoord coord) const noexcept;

    /** @brief Returns the underlying CSR representation. @return The underlying CSR representation. @ingroup hbrick_graph */
    [[nodiscard]] const CsrGraph& csrGraph() const noexcept { return graph_; }

    /**
     * @brief Wraps an existing CSR graph with grid coordinate metadata.
     * @ingroup hbrick_graph
     */
    [[nodiscard]] static DirectedGridGraph fromCsr(
        uint32_t width,
        uint32_t height,
        CsrGraph graph
    );

    /**
     * @brief Materializes all directed edges with endpoint indices.
     * @ingroup hbrick_graph
     *
     * @return Edge list copied from the internal CSR storage.
     */
    [[nodiscard]] std::vector<Edge32> edges() const;

private:
    friend class DirectedGridGraphBuilder;

    DirectedGridGraph(uint32_t width, uint32_t height, CsrGraph graph);

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    CsrGraph graph_;
};

}  // namespace hbrick
