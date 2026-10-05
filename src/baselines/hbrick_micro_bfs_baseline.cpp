/**
 * @file hbrick_micro_bfs_baseline.cpp
 * @brief Implementation of HBrickMicroBfsBaseline.
 */

#include "hbrick/baselines/hbrick_micro_bfs_baseline.hpp"

namespace hbrick {

void HBrickMicroBfsBaseline::preprocess(
    const DirectedGridGraph& graph,
    const MazeLayout& layout,
    HBrickConfig config
) {
    config.omit_local_closure = true;
    hbrick_.preprocess(graph, layout, config);
}

ReachabilityAnswer HBrickMicroBfsBaseline::query(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    return hbrick_.query(source, target);
}

ReachabilityAnswer HBrickMicroBfsBaseline::query(
    const uint32_t source,
    const uint32_t target,
    HBrickQueryScratch& scratch,
    GraphSearchScratch& port_bfs_scratch,
    GraphSearchScratch& micro_bfs_scratch
) const noexcept {
    return hbrick_.query(source, target, scratch, port_bfs_scratch, micro_bfs_scratch);
}

HBrickQueryOutcome HBrickMicroBfsBaseline::queryDetailed(
    const uint32_t source,
    const uint32_t target
) const noexcept {
    return hbrick_.queryDetailed(source, target);
}

HBrickQueryOutcome HBrickMicroBfsBaseline::queryDetailed(
    const uint32_t source,
    const uint32_t target,
    HBrickQueryScratch& scratch,
    GraphSearchScratch& port_bfs_scratch,
    GraphSearchScratch& micro_bfs_scratch
) const noexcept {
    return hbrick_.queryDetailed(source, target, scratch, port_bfs_scratch, micro_bfs_scratch);
}

}  // namespace hbrick
