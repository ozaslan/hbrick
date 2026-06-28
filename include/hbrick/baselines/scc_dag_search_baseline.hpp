/**
 * @file scc_dag_search_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief SCC condensation plus per-query DAG search baseline.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/scc_decomposition.hpp"

namespace hbrick {

/**
 * @brief Reference baseline that searches the condensation DAG per query.
 * @ingroup hbrick_baselines
 *
 * Preprocessing computes SCCs and stores the component DAG. Each query maps
 * endpoints to components and runs @ref hbrick::DagReachability. Trades higher query
 * cost for lower memory use than @ref hbrick::SccDagClosureBaseline.
 */
class SccDagSearchBaseline {
public:
    /**
     * @brief Computes SCCs and builds the condensation DAG for @p graph.
     * @ingroup hbrick_baselines
     */
    void preprocess(const CsrGraph& graph, GraphSearchScratch& scratch);

    /**
     * @brief Begins incremental preprocessing for UI-driven benchmark stepping.
     * @ingroup hbrick_baselines
     */
    void beginPreprocess(const CsrGraph& graph, GraphSearchScratch& scratch);

    /**
     * @brief Advances incremental preprocessing by up to @p vertex_batch outer steps.
     * @return @c true when preprocessing has completed.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] bool preprocessStep(
        GraphSearchScratch& scratch,
        uint32_t vertex_batch
    ) noexcept;

    /** @brief Aborts an in-progress incremental preprocess. @ingroup hbrick_baselines */
    void cancelPreprocess() noexcept;

    /** @brief Completed preprocess work units for live benchmark progress. @ingroup hbrick_baselines */
    [[nodiscard]] uint64_t preprocessWorkCompleted() const noexcept {
        return preprocess_work_completed_;
    }

    /** @brief Total preprocess work units for live benchmark progress. @ingroup hbrick_baselines */
    [[nodiscard]] uint64_t preprocessWorkTotal() const noexcept {
        return preprocess_work_total_;
    }

    /**
     * @brief Answers reachability by searching the condensation DAG.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target,
        GraphSearchScratch& scratch
    ) const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

private:
    enum class PreprocessPhase : uint8_t {
        Idle,
        FirstPass,
        Transpose,
        SecondPass,
        Condensation,
        Done,
    };

    void resetPreprocessState() noexcept;
    void runFirstPassBatch(GraphSearchScratch& scratch, uint32_t vertex_batch) noexcept;
    void runSecondPassBatch(GraphSearchScratch& scratch, uint32_t vertex_batch) noexcept;
    void runCondensationBatch(uint32_t vertex_batch) noexcept;

    BaselineStatus status_ = BaselineStatus::NotRun;
    PreprocessPhase preprocess_phase_ = PreprocessPhase::Idle;
    const CsrGraph* preprocess_graph_ = nullptr;
    uint32_t first_pass_cursor_ = 0U;
    std::size_t second_pass_cursor_ = 0U;
    uint32_t condensation_cursor_ = 0U;
    uint32_t visit_mark_ = 0U;
    std::vector<uint32_t> finish_order_;
    std::vector<uint32_t> component_ids_;
    CsrGraph transpose_graph_;
    CsrGraphBuilder condensation_builder_{0U};
    uint32_t num_components_ = 0U;
    uint64_t preprocess_work_completed_ = 0U;
    uint64_t preprocess_work_total_ = 0U;
    SccDecomposition decomposition_;
    CsrGraph condensation_dag_;
};

}  // namespace hbrick
