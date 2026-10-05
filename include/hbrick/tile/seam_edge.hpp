/**
 * @file seam_edge.hpp
 * @ingroup hbrick_tile
 * @brief Directed cross-tile seam edge between two global port vertices.
 */

#pragma once

#include <cstdint>

namespace hbrick {

/**
 * @brief One directed seam edge in the global port graph.
 * @ingroup hbrick_tile
 */
struct SeamEdge {
    /** @brief Source port id in @ref PortIndex. @ingroup hbrick_tile */
    uint32_t from_port_id = 0U;
    /** @brief Destination port id in @ref PortIndex. @ingroup hbrick_tile */
    uint32_t to_port_id = 0U;
};

}  // namespace hbrick
