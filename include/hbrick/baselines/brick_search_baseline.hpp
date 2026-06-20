/**
 * @file brick_search_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief Flat BRICK reachability via port-graph BFS and tile attachments.
 */

#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/**
 * @brief BRICK-Search baseline: port-graph BFS with @c R_VB / @c R_BV attachments.
 * @ingroup hbrick_baselines
 *
 * Preprocessing builds a @ref BrickIndex (base tiles + global port CSR). Each query
 * applies the same-tile local-closure shortcut, seeds port BFS from @c R_VB, and
 * succeeds when a visited port satisfies @c R_BV to the target cell.
 */
class BrickSearchBaseline {
public:
    /**
     * @brief Builds the flat-BRICK index for @p graph.
     * @ingroup hbrick_baselines
     */
    void preprocess(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        TileSize nominal_tile_size,
        uint64_t max_memory_bytes
    );

    /**
     * @brief Answers reachability using BRICK-Search on the port graph.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] ReachabilityAnswer query(uint32_t source, uint32_t target) const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

    /** @brief Returns the built index when preprocessing completed. @ingroup hbrick_baselines */
    [[nodiscard]] const BrickIndex& index() const noexcept { return index_; }

    /**
     * @brief Returns port-graph BFS scratch sized during the last successful preprocess.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] GraphSearchScratch& portScratch() noexcept { return port_scratch_; }
    [[nodiscard]] const GraphSearchScratch& portScratch() const noexcept {
        return port_scratch_;
    }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    BrickIndex index_{};
    mutable GraphSearchScratch port_scratch_{};
};

}  // namespace hbrick
