#include "hbrick/tile/port_index.hpp"

#include <algorithm>
#include <cassert>
#include <limits>

#include "hbrick/tile/brick_tile_index.hpp"

namespace hbrick {

PortIndex PortIndex::build(
    const BrickTileIndex& tile_index,
    const uint32_t num_global_vertices
) {
    PortIndex index;
    index.num_tiles_ = tile_index.decomposition().numSlots();

    uint32_t total_ports = 0U;
    for (const BaseTileSummary& summary : tile_index.summaries()) {
        if (summary.status == BaselineStatus::Completed) {
            total_ports += summary.numPorts();
        }
    }

    index.ports_.reserve(total_ports);
    index.global_vertex_to_port_id_.assign(
        num_global_vertices,
        std::numeric_limits<uint32_t>::max()
    );
    index.tile_port_offsets_.assign(
        static_cast<std::size_t>(index.num_tiles_) + 1U,
        0U
    );

    uint32_t current_port_id = 0U;
    for (uint32_t tile_index_value = 0U; tile_index_value < index.num_tiles_; ++tile_index_value) {
        index.tile_port_offsets_[tile_index_value] = current_port_id;
        const BaseTileSummary& summary = tile_index.summaryByIndex(tile_index_value);
        if (summary.status != BaselineStatus::Completed) {
            continue;
        }

        for (uint32_t tile_port_index = 0U; tile_port_index < summary.numPorts(); ++tile_port_index) {
            const TilePort& tile_port = summary.ports[tile_port_index];
            assert(tile_port.local_index < summary.global_vertices.size());
            const uint32_t global_vertex =
                summary.global_vertices[tile_port.local_index];

            PortRecord record{};
            record.coord = tile_port.coord;
            record.global_vertex = global_vertex;
            record.tile_index = tile_index_value;
            record.tile_port_index = tile_port_index;

            index.ports_.push_back(record);
            index.global_vertex_to_port_id_[global_vertex] = current_port_id;
            ++current_port_id;
        }
    }
    index.tile_port_offsets_[index.num_tiles_] = current_port_id;

    return index;
}

const PortRecord& PortIndex::port(const uint32_t port_id) const noexcept {
    assert(port_id < ports_.size());
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
    if (tile_index_value >= num_tiles_) {
        return std::numeric_limits<uint32_t>::max();
    }
    const uint32_t start = tile_port_offsets_[tile_index_value];
    const uint32_t count = tile_port_offsets_[tile_index_value + 1U] - start;
    if (tile_port_index >= count) {
        return std::numeric_limits<uint32_t>::max();
    }
    return start + tile_port_index;
}

uint64_t PortIndex::estimateStorageBytes() const noexcept {
    return static_cast<uint64_t>(ports_.size()) * sizeof(PortRecord)
        + static_cast<uint64_t>(global_vertex_to_port_id_.size()) * sizeof(uint32_t)
        + static_cast<uint64_t>(tile_port_offsets_.size()) * sizeof(uint32_t);
}

uint64_t PortIndex::estimateBuildStorageBytes(
    const BrickTileIndex& tile_index,
    const uint32_t num_global_vertices
) noexcept {
    uint32_t num_ports = 0U;
    const uint32_t num_tiles = tile_index.decomposition().numSlots();
    for (const BaseTileSummary& summary : tile_index.summaries()) {
        if (summary.status == BaselineStatus::Completed) {
            num_ports += summary.numPorts();
        }
    }

    return static_cast<uint64_t>(num_ports) * sizeof(PortRecord)
        + static_cast<uint64_t>(num_global_vertices) * sizeof(uint32_t)
        + static_cast<uint64_t>(num_tiles + 1U) * sizeof(uint32_t);
}

}  // namespace hbrick
