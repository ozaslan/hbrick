/**
 * @file two_hop_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief Cohen-style 2-hop reachability labeling baseline.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"

namespace hbrick {

/**
 * @brief Exact 2-hop reachability index using all-vertex hub labeling.
 * @ingroup hbrick_baselines
 *
 * Preprocessing assigns hub labels to outgoing and incoming label sets so that
 * @c source reaches @c target iff the label sets intersect. Construction follows
 * the Cohen et al. hub-covering scheme with every vertex as a hub.
 */
class TwoHopBaseline {
public:
    /**
     * @brief Builds outgoing and incoming 2-hop labels for @p graph when memory allows.
     * @ingroup hbrick_baselines
     *
     * @param graph Input directed graph.
     * @param scratch Reusable workspace for hub reachability scans.
     * @param max_memory_bytes Upper bound on stored label bytes.
     */
    void preprocess(
        const CsrGraph& graph,
        GraphSearchScratch& scratch,
        uint64_t max_memory_bytes
    );

    /**
     * @brief Answers reachability via sorted label-set intersection.
     * @ingroup hbrick_baselines
     *
     * @param source Source vertex index.
     * @param target Target vertex index.
     * @return Reachability result derived from 2-hop labels.
     */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target
    ) const noexcept;

    /**
     * @brief Estimates the worst-case label storage for @p num_vertices hubs.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] static uint64_t estimateMaxLabelBytes(
        uint32_t num_vertices
    ) noexcept;

    /**
     * @brief Returns stored label bytes after successful preprocessing.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] uint64_t labelStorageBytes() const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    uint32_t num_vertices_ = 0U;
    std::vector<std::vector<uint32_t>> labels_out_;
    std::vector<std::vector<uint32_t>> labels_in_;
};

}  // namespace hbrick
