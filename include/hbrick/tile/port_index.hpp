/**
 * @file port_index.hpp
 * @ingroup hbrick_tile
 * @brief Global bijection between port ids, grid coordinates, and graph vertices.
 */

#pragma once

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "hbrick/core/grid_coord.hpp"

namespace hbrick {

class BrickTileIndex;

/**
 * @brief Metadata for one port vertex in the flat BRICK port graph.
 * @ingroup hbrick_tile
 */
struct PortRecord {
    /** @brief Fine-grid coordinate of the boundary cell. @ingroup hbrick_tile */
    GridCoord coord{};
    /** @brief Global directed-grid vertex id. @ingroup hbrick_tile */
    uint32_t global_vertex = 0U;
    /** @brief Linear slot index in the owning @ref BrickTileIndex. @ingroup hbrick_tile */
    uint32_t tile_index = 0U;
    /** @brief Index into the tile's @c ports[] array. @ingroup hbrick_tile */
    uint32_t tile_port_index = 0U;
};

/**
 * @brief Maps port graph vertex ids to spatial identity and tile-local indices.
 * @ingroup hbrick_tile
 */
class PortIndex {
public:
    /**
     * @brief Builds a global port list from completed tile summaries.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] static PortIndex build(
        const BrickTileIndex& tile_index,
        uint32_t num_global_vertices
    );

    /** @brief Number of ports in the global port graph. @ingroup hbrick_tile */
    [[nodiscard]] uint32_t numPorts() const noexcept {
        return static_cast<uint32_t>(ports_.size());
    }

    /** @brief Read-only view of all port records. @ingroup hbrick_tile */
    [[nodiscard]] std::span<const PortRecord> ports() const noexcept {
        return ports_;
    }

    /** @brief Returns port metadata for @p port_id. @ingroup hbrick_tile */
    [[nodiscard]] const PortRecord& port(uint32_t port_id) const noexcept;

    /**
     * @brief Returns the global port id for a graph vertex.
     * @ingroup hbrick_tile
     *
     * @return Port id, or @c UINT32_MAX when @p global_vertex is not a port.
     */
    [[nodiscard]] uint32_t portIdForGlobalVertex(uint32_t global_vertex) const noexcept;

    /**
     * @brief Returns the global port id for a tile-local port index.
     * @ingroup hbrick_tile
     *
     * @return Port id, or @c UINT32_MAX when the lookup is invalid.
     */
    [[nodiscard]] uint32_t portIdForTilePort(
        uint32_t tile_index,
        uint32_t tile_port_index
    ) const noexcept;

private:
    std::vector<PortRecord> ports_{};
    std::vector<uint32_t> global_vertex_to_port_id_{};
    std::vector<uint32_t> tile_port_lookup_{};
    uint32_t num_tiles_ = 0U;
    uint32_t max_ports_per_tile_ = 0U;
};

}  // namespace hbrick
