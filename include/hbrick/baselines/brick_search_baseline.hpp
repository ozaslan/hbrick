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
#include "hbrick/tile/brick_index_builder.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/**
 * @brief BRICK-Search baseline: port-graph BFS with @c R_VB / @c R_BV attachments.
 * @ingroup hbrick_baselines
 */
class BrickSearchBaseline {
public:
    /** @brief Builds the flat-BRICK index for @p graph. @ingroup hbrick_baselines */
    void preprocess(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        TileSize nominal_tile_size,
        uint64_t max_memory_bytes
    );

    /** @brief Starts incremental index preprocessing. @ingroup hbrick_baselines */
    void beginPreprocess(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        TileSize nominal_tile_size,
        uint64_t max_memory_bytes
    );

    /**
     * @brief Advances preprocessing by one index-build step.
     * @return @c true when preprocessing finished or is not active.
     */
    [[nodiscard]] bool preprocessStep() noexcept;

    /** @brief Aborts incremental preprocessing. @ingroup hbrick_baselines */
    void cancelPreprocess() noexcept;

    [[nodiscard]] bool preprocessActive() const noexcept;
    [[nodiscard]] uint64_t preprocessWorkCompleted() const noexcept {
        return preprocess_work_completed_;
    }
    [[nodiscard]] uint64_t preprocessWorkTotal() const noexcept {
        return preprocess_work_total_;
    }

    [[nodiscard]] ReachabilityAnswer query(uint32_t source, uint32_t target) const noexcept;

    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

    [[nodiscard]] const BrickIndex& index() const noexcept { return index_; }

    /** @brief Bytes charged on the preprocess ledger (includes partial work on skip). @ingroup hbrick_baselines */
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept;

    [[nodiscard]] GraphSearchScratch& portScratch() noexcept { return port_scratch_; }
    [[nodiscard]] const GraphSearchScratch& portScratch() const noexcept {
        return port_scratch_;
    }

private:
    void resetPreprocessState() noexcept;

    BaselineStatus status_ = BaselineStatus::NotRun;
    BrickIndex index_{};
    mutable GraphSearchScratch port_scratch_{};
    BrickIndexBuilder index_builder_{};
    bool preprocess_active_ = false;
    uint64_t preprocess_work_completed_ = 0U;
    uint64_t preprocess_work_total_ = 0U;
    uint32_t num_vertices_ = 0U;
};

}  // namespace hbrick
