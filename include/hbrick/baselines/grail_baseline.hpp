/**
 * @file grail_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief GRAIL reachability baseline: randomized-DFS interval labels on the SCC DAG.
 *
 * Wraps the official GRAIL implementation by Hilmi Yildirim and Mohammed J. Zaki
 * (VLDB 2010 / ACM JEA).
 */

#pragma once

#include <cstdint>
#include <memory>

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
    /** @brief Number of randomized interval labelings on the condensation. */
    uint32_t num_trees = 5U;
    /** @brief Seed for randomized DFS root and out-edge order. */
    uint64_t seed = 0x475241494CULL;
    /** @brief Labeling type: 0: randomized, 1: reverse pairs, 2-5: heuristic. */
    int labeling_type = 0;
    /** @brief Search algorithm: 1: basic search, 2: level filter, -2: positive cut + level filter. */
    int alg_type = 1;
};

/**
 * @brief Detailed outcome of a @ref GrailBaseline query.
 * @ingroup hbrick_baselines
 */
struct GrailQueryOutcome {
    /** @brief Reachability answer. */
    ReachabilityAnswer answer = ReachabilityAnswer::Unreachable;
    /**
     * @brief @c true when labels alone settled the query.
     *
     * Same-component positives and interval non-containment negatives are
     * label-resolved. Guided condensation search sets this @c false.
     */
    bool tree_certified = false;
};

struct GrailState;

/**
 * @brief Canonical GRAIL: randomized-DFS condensation intervals, pruning, guided search.
 * @ingroup hbrick_baselines
 *
 * Preprocessing contracts @p G to its SCC condensation DAG and builds @c k
 * interval labelings using the official GRAIL implementation (Yildirim et al.).
 * A query is answered immediately when endpoints share an SCC or when an
 * interval test rejects reachability (sound negative). Remaining pairs are
 * resolved using pruned DAG search.
 */
class GrailBaseline {
public:
    GrailBaseline();
    ~GrailBaseline();

    GrailBaseline(GrailBaseline&&) noexcept;
    GrailBaseline& operator=(GrailBaseline&&) noexcept;

    GrailBaseline(const GrailBaseline&) = delete;
    GrailBaseline& operator=(const GrailBaseline&) = delete;

    /**
     * @brief Builds condensation interval labels for @p graph when memory allows.
     *
     * @param graph Input directed graph.
     * @param params Labeling configuration.
     * @param max_memory_bytes Upper bound on stored label and component bytes.
     */
    void preprocess(
        const CsrGraph& graph,
        const GrailBaselineParams& params,
        uint64_t max_memory_bytes
    );

    /**
     * @brief Answers reachability using GRAIL pruning and guided search.
     *
     * @param source Source vertex index.
     * @param target Target vertex index.
     * @param scratch Reusable traversal workspace.
     * @return Exact reachability result.
     */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) const noexcept;

    /**
     * @brief Answers reachability and reports whether labels alone resolved it.
     */
    [[nodiscard]] GrailQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) const noexcept;

    /**
     * @brief Conservative label-storage estimate using @p num_vertices components.
     */
    [[nodiscard]] static uint64_t estimateLabelBytes(
        uint32_t num_vertices,
        uint32_t num_trees
    ) noexcept;

    /**
     * @brief Query-resident bytes: component map, condensation DAG, and interval labels.
     */
    [[nodiscard]] uint64_t labelStorageBytes() const noexcept;

    /**
     * @brief Synonym for @ref labelStorageBytes() for index benchmark accounting.
     */
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept {
        return labelStorageBytes();
    }

    /**
     * @brief Retained storage bytes for benchmark reporting.
     */
    [[nodiscard]] uint64_t measuredStorageBytes() const noexcept {
        return labelStorageBytes();
    }

    /**
     * @brief Returns whether every tree interval of @p source contains @p target.
     *
     * Containment is necessary for reachability; non-containment is a sound negative.
     */
    [[nodiscard]] bool intervalLabelsContain(uint32_t source, uint32_t target) const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    uint32_t num_vertices_ = 0U;
    std::unique_ptr<GrailState> state_;
};

}  // namespace hbrick
