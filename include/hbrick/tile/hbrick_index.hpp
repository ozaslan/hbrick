/**
 * @file hbrick_index.hpp
 * @ingroup hbrick_tile
 * @brief Complete H-BRICK preprocess product: base tiles, hierarchy, and super summaries.
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "hbrick/core/types.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/hbrick_build_report.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/super_tile_summary.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/**
 * @brief Resident-storage estimate split between flat BRICK and hierarchical extras.
 * @ingroup hbrick_tile
 */
struct HBrickStorageEstimate {
    /** @brief Flat BRICK: L0 closures, attachments, port index, port CSR, seams. @ingroup hbrick_tile */
    uint64_t brick_bytes = 0U;
    /** @brief H-BRICK hierarchy: composed super-tile closure and boundary data. @ingroup hbrick_tile */
    uint64_t hbrick_extra_bytes = 0U;

    /** @brief Sum of @ref brick_bytes and @ref hbrick_extra_bytes. @ingroup hbrick_tile */
    [[nodiscard]] uint64_t totalBytes() const noexcept {
        return brick_bytes + hbrick_extra_bytes;
    }
};

/**
 * @brief H-BRICK index owning flat base tiles plus bottom-up composed super summaries.
 * @ingroup hbrick_tile
 */
class HBrickIndex {
public:
    /**
     * @brief Builds the full H-BRICK preprocess product for a map.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] static HBrickIndex build(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        HBrickConfig config
    );

    /** @brief Outcome of building this index. @ingroup hbrick_tile */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

    /** @brief Configuration used to build this index. @ingroup hbrick_tile */
    [[nodiscard]] const HBrickConfig& config() const noexcept { return config_; }

    /** @brief Level-0 flat BRICK base tiles, ports, and seam edges. @ingroup hbrick_tile */
    [[nodiscard]] const BrickIndex& brickIndex() const noexcept { return brick_index_; }

    /** @brief Region grouping tree over base and super tile slots. @ingroup hbrick_tile */
    [[nodiscard]] const HierarchyTree& hierarchy() const noexcept { return hierarchy_; }

    /**
     * @brief Returns whether composed super summaries exist for @p level.
     * @ingroup hbrick_tile
     *
     * @return @c true for hierarchy levels @c >= 1 that were built.
     */
    [[nodiscard]] bool hasSuperLevel(uint32_t level) const noexcept;

    /**
     * @brief Returns the composed summary for one super-level region node.
     * @ingroup hbrick_tile
     *
     * @param level Hierarchy level (@c >= 1).
     * @param node_index Row-major node index at @p level.
     */
    [[nodiscard]] const SuperTileSummary& superSummary(
        uint32_t level,
        uint32_t node_index
    ) const noexcept;

    /**
     * @brief Read-only view of all super summaries at @p level.
     * @ingroup hbrick_tile
     *
     * @param level Hierarchy level (@c >= 1).
     */
    [[nodiscard]] std::span<const SuperTileSummary> superLevel(
        uint32_t level
    ) const noexcept;

    /**
     * @brief Estimates resident storage for closures and boundary summaries.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] uint64_t estimateStorageBytes() const noexcept;

    /**
     * @brief Estimates flat-BRICK vs hierarchical storage separately.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] HBrickStorageEstimate estimateStorageBreakdown() const noexcept;

    /**
     * @brief Returns port-graph and super-interface graph vertex counts per level.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] std::vector<HBrickLevelGraphStats> levelGraphStats() const noexcept;

private:
    friend class HBrickIndexBuilder;
    BaselineStatus status_ = BaselineStatus::NotRun;
    HBrickConfig config_{};
    BrickIndex brick_index_{};
    HierarchyTree hierarchy_{};
    std::vector<std::vector<SuperTileSummary>> super_summaries_{};
};

}  // namespace hbrick
