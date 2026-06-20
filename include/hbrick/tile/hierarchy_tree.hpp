/**
 * @file hierarchy_tree.hpp
 * @ingroup hbrick_tile
 * @brief Multi-level region hierarchy for H-BRICK preprocessing.
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/tile/group_size.hpp"
#include "hbrick/tile/region_node.hpp"
#include "hbrick/tile/tile_decomposition.hpp"

namespace hbrick {

/**
 * @brief Bottom-up region tree over base tile slots and grouped super-regions.
 * @ingroup hbrick_tile
 */
class HierarchyTree {
public:
    /**
     * @brief Builds a hierarchy up to @p max_depth levels or a single root.
     * @ingroup hbrick_tile
     *
     * Level @c 0 mirrors @p base_decomposition tile slots. Each subsequent level
     * groups the previous level's slot grid by @p group_size.
     */
    [[nodiscard]] static HierarchyTree build(
        const TileDecomposition& base_decomposition,
        GroupSize group_size,
        uint32_t max_depth
    );

    /** @brief Base (level-0) tile decomposition. @ingroup hbrick_tile */
    [[nodiscard]] const TileDecomposition& baseDecomposition() const noexcept {
        return base_decomposition_;
    }

    /** @brief Grouping factor used above level @c 0. @ingroup hbrick_tile */
    [[nodiscard]] GroupSize groupSize() const noexcept { return group_size_; }

    /** @brief Number of stored hierarchy levels (including level @c 0). @ingroup hbrick_tile */
    [[nodiscard]] uint32_t numLevels() const noexcept {
        return static_cast<uint32_t>(levels_.size());
    }

    /** @brief All region nodes at @p level_index. @ingroup hbrick_tile */
    [[nodiscard]] std::span<const RegionNode> level(uint32_t level_index) const noexcept;

    /** @brief Returns one node by level and index. @ingroup hbrick_tile */
    [[nodiscard]] const RegionNode& node(
        uint32_t level_index,
        uint32_t node_index
    ) const noexcept;

private:
    TileDecomposition base_decomposition_{};
    GroupSize group_size_{};
    std::vector<std::vector<RegionNode>> levels_{};
};

}  // namespace hbrick
