/**
 * @file hbrick_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief Hierarchical H-BRICK reachability via attachment propagation and ancestor tests.
 */

#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/baselines/hbrick_query_scratch.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hbrick_index.hpp"
#include "hbrick/tile/brick_index.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;
class GraphSearchScratch;

/**
 * @brief H-BRICK baseline: hierarchical boolean propagation with ancestor meet tests.
 * @ingroup hbrick_baselines
 */
class HBrickBaseline {
public:
    /**
     * @brief Builds the H-BRICK index for @p graph.
     * @ingroup hbrick_baselines
     */
    void preprocess(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        HBrickConfig config
    );

    /**
     * @brief Answers reachability using hierarchical H-BRICK query steps.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        HBrickQueryScratch& scratch,
        GraphSearchScratch& port_bfs_scratch
    ) const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

    /** @brief Returns the built index when preprocessing completed. @ingroup hbrick_baselines */
    [[nodiscard]] const HBrickIndex& index() const noexcept { return index_; }

    /**
     * @brief Returns hierarchical summary storage bytes after successful preprocessing.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept;

    /** @brief Returns query scratch sized during the last successful preprocess. @ingroup hbrick_baselines */
    [[nodiscard]] HBrickQueryScratch& scratch() noexcept { return scratch_; }
    [[nodiscard]] const HBrickQueryScratch& scratch() const noexcept { return scratch_; }

private:
    [[nodiscard]] static ReachabilityAnswer queryHierarchical(
        const HBrickIndex& index,
        uint32_t source,
        uint32_t target,
        HBrickQueryScratch& scratch
    ) noexcept;

    [[nodiscard]] static ReachabilityAnswer queryFlatBrickPortBfs(
        const BrickIndex& index,
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) noexcept;

    BaselineStatus status_ = BaselineStatus::NotRun;
    HBrickIndex index_{};
    HBrickQueryScratch scratch_{};
};

}  // namespace hbrick
