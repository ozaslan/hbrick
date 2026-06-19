/**
 * @file grail_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief GRAIL reachability filter with BFS verification baseline.
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
 * @brief Parameters for @ref hbrick::GrailBaseline preprocessing.
 * @ingroup hbrick_baselines
 */
struct GrailBaselineParams {
    /** @brief Number of random DFS labelings to build. @ingroup hbrick_baselines */
    uint32_t num_trees = 5U;
    /** @brief Seed for deterministic random permutations. @ingroup hbrick_baselines */
    uint64_t seed = 0x475241494CULL;
};

/**
 * @brief Detailed outcome of a @ref GrailBaseline query.
 * @ingroup hbrick_baselines
 */
struct GrailQueryOutcome {
    /** @brief Reachability answer. @ingroup hbrick_baselines */
    ReachabilityAnswer answer = ReachabilityAnswer::Unreachable;
    /** @brief @c true when a DFS tree interval certified reachability. @ingroup hbrick_baselines */
    bool tree_certified = false;
};

/**
 * @brief GRAIL positive filter with exact BFS fallback for remaining queries.
 * @ingroup hbrick_baselines
 *
 * Preprocessing builds multiple random DFS interval labelings. Queries that pass
 * an interval test are reachable via a tree path; all other queries fall back to BFS.
 */
class GrailBaseline {
public:
    /**
     * @brief Builds GRAIL interval labels for @p graph when memory allows.
     * @ingroup hbrick_baselines
     *
     * @param graph Input directed graph.
     * @param params Random labeling configuration.
     * @param max_memory_bytes Upper bound on stored label bytes.
     */
    void preprocess(
        const CsrGraph& graph,
        const GrailBaselineParams& params,
        uint64_t max_memory_bytes
    );

    /**
     * @brief Answers reachability using GRAIL filtering and BFS verification.
     * @ingroup hbrick_baselines
     *
     * @param source Source vertex index.
     * @param target Target vertex index.
     * @param scratch Reusable traversal workspace for BFS verification.
     * @return Exact reachability result.
     */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) const noexcept;

    /**
     * @brief Answers reachability and reports whether a tree interval certified it.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] GrailQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) const noexcept;

    /**
     * @brief Estimates label storage for @p num_vertices and @p num_trees labelings.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] static uint64_t estimateLabelBytes(
        uint32_t num_vertices,
        uint32_t num_trees
    ) noexcept;

    /**
     * @brief Returns stored interval-label bytes after successful preprocessing.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] uint64_t labelStorageBytes() const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

private:
    struct TreeLabeling {
        std::vector<uint32_t> pre;
        std::vector<uint32_t> post;
    };

    [[nodiscard]] static bool intervalContains(
        const TreeLabeling& labeling,
        uint32_t source,
        uint32_t target
    ) noexcept;

    BaselineStatus status_ = BaselineStatus::NotRun;
    CsrGraph graph_;
    std::vector<TreeLabeling> labelings_;
};

}  // namespace hbrick
