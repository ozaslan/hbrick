#include "hbrick/tile/port_index.hpp"

#include <algorithm>
#include <limits>

#include "hbrick/tile/brick_tile_index.hpp"

namespace hbrick {

PortIndex PortIndex::build(
    const BrickTileIndex& tile_index,
    const uint32_t num_global_vertices
) {
    PortIndex index;
    index.num_tiles_ = tile_index.decomposition().numSlots();
    index.max_ports_per_tile_ = 0U;

    for (const BaseTileSummary& summary : tile_index.summaries()) {
        if (summary.status != BaselineStatus::Completed) {
            continue;
        }
        index.max_ports_per_tile_ = std::max(index.max_ports_per_tile_, summary.numPorts());
    }

    index.global_vertex_to_port_id_.assign(
        num_global_vertices,
        std::numeric_limits<uint32_t>::max()
    );
    index.tile_port_lookup_.assign(
        static_cast<std::size_t>(index.num_tiles_) *
            static_cast<std::size_t>(index.max_ports_per_tile_),
        std::numeric_limits<uint32_t>::max()
    );

    for (uint32_t tile_index_value = 0U; tile_index_value < index.num_tiles_; ++tile_index_value) {
        const BaseTileSummary& summary = tile_index.summaryByIndex(tile_index_value);
        if (summary.status != BaselineStatus::Completed) {
            continue;
        }

        for (uint32_t tile_port_index = 0U; tile_port_index < summary.numPorts(); ++tile_port_index) {
            const TilePort& tile_port = summary.ports[tile_port_index];
            const uint32_t global_vertex =
                summary.global_vertices[tile_port.local_index];

            PortRecord record{};
            record.coord = tile_port.coord;
            record.global_vertex = global_vertex;
            record.tile_index = tile_index_value;
            record.tile_port_index = tile_port_index;

            const uint32_t port_id = static_cast<uint32_t>(index.ports_.size());
            index.ports_.push_back(record);
            index.global_vertex_to_port_id_[global_vertex] = port_id;
            index.tile_port_lookup_[static_cast<std::size_t>(tile_index_value) *
                                         static_cast<std::size_t>(index.max_ports_per_tile_) +
                                     static_cast<std::size_t>(tile_port_index)] = port_id;
        }
    }

    return index;
}

const PortRecord& PortIndex::port(const uint32_t port_id) const noexcept {
    return ports_[port_id];
}

uint32_t PortIndex::portIdForGlobalVertex(const uint32_t global_vertex) const noexcept {
    if (global_vertex >= global_vertex_to_port_id_.size()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return global_vertex_to_port_id_[global_vertex];
}

uint32_t PortIndex::portIdForTilePort(
    const uint32_t tile_index_value,
    const uint32_t tile_port_index
) const noexcept {
    if (tile_index_value >= num_tiles_ || tile_port_index >= max_ports_per_tile_) {
        return std::numeric_limits<uint32_t>::max();
    }
    const std::size_t lookup_index =
        static_cast<std::size_t>(tile_index_value) *
            static_cast<std::size_t>(max_ports_per_tile_) +
        static_cast<std::size_t>(tile_port_index);
    return tile_port_lookup_[lookup_index];
}

uint64_t PortIndex::estimateStorageBytes() const noexcept {
    return static_cast<uint64_t>(ports_.size()) * sizeof(PortRecord)
        + static_cast<uint64_t>(global_vertex_to_port_id_.size()) * sizeof(uint32_t)
        + static_cast<uint64_t>(tile_port_lookup_.size()) * sizeof(uint32_t);
}

}  // namespace hbrick
