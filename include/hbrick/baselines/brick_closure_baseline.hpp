/**
 * @file brick_closure_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief Flat BRICK reachability via precomputed port-graph transitive closure.
 */

#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/bit/kleene_squaring_options.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/brick_index_builder.hpp"
#include "hbrick/tile/preprocess_memory_ledger.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/**
 * @brief BRICK-Closure baseline: truncated Kleene closure on the global port graph + attachments.
 * @ingroup hbrick_baselines
 *
 * Preprocessing builds a @ref BrickIndex (base tiles + global port CSR) and then
 * materializes a reflexive transitive closure over the port graph via truncated
 * Kleene squaring. When @c C < @c V, closure is computed on the SCC-compressed
 * @c C x @c C component graph and expanded back to ports.
 *
 * Query first applies the same-tile local-closure shortcut; otherwise it answers
 * whether there exist ports @c p,q such that @c R_VB(source,p) and
 * @c port_closure[p,q] and @c R_BV(q,target).
 */
class BrickClosureBaseline {
public:
    /**
     * @brief Builds the flat-BRICK index and port-graph closure when memory allows.
     * @ingroup hbrick_baselines
     */
    void preprocess(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        TileSize nominal_tile_size,
        uint64_t max_memory_bytes,
        KleeneSquaringOptions kleene_options = {}
    );

    /**
     * @brief Starts incremental preprocessing for UI-driven benchmarks.
     * @ingroup hbrick_baselines
     */
    void beginPreprocess(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        TileSize nominal_tile_size,
        uint64_t max_memory_bytes,
        KleeneSquaringOptions kleene_options = {}
    );

    /**
     * @brief Advances preprocessing by a bounded amount of work.
     * @ingroup hbrick_baselines
     *
     * @return @c true when preprocessing finished or is not active.
     */
    [[nodiscard]] bool preprocessStep() noexcept;

    /** @brief Aborts incremental preprocessing. @ingroup hbrick_baselines */
    void cancelPreprocess() noexcept;

    /** @brief Returns @c true while incremental preprocessing is active. @ingroup hbrick_baselines */
    [[nodiscard]] bool preprocessActive() const noexcept;

    /** @brief Completed preprocess work units during incremental preprocessing. @ingroup hbrick_baselines */
    [[nodiscard]] uint64_t preprocessWorkCompleted() const noexcept {
        return preprocess_work_completed_;
    }

    /** @brief Total preprocess work units for the active incremental run. @ingroup hbrick_baselines */
    [[nodiscard]] uint64_t preprocessWorkTotal() const noexcept {
        return preprocess_work_total_;
    }

    /**
     * @brief Answers reachability using BRICK-Closure (no port BFS at query time).
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target
    ) const noexcept;

    /** @brief Returns the outcome of the most recent preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

    /** @brief Returns the built index when preprocessing completed. @ingroup hbrick_baselines */
    [[nodiscard]] const BrickIndex& index() const noexcept { return index_; }

    /** @brief Returns bytes charged on the shared preprocess ledger after preprocessing. @ingroup hbrick_baselines */
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept;

    /** @brief Returns the precomputed port-graph closure after successful preprocessing. @ingroup hbrick_baselines */
    [[nodiscard]] const BitMatrix& portClosure() const noexcept { return port_closure_; }

    /** @brief Returns the Kleene squaring options used during preprocessing. @ingroup hbrick_baselines */
    [[nodiscard]] const KleeneSquaringOptions& kleeneOptions() const noexcept {
        return kleene_options_;
    }

    /** @brief Effective Kleene worker count for the active or last preprocess run. @ingroup hbrick_baselines */
    [[nodiscard]] uint32_t kleeneThreadCount() const noexcept {
        return kleene_thread_count_;
    }

    /** @brief Scheduled Kleene squaring rounds for the last preprocess run. @ingroup hbrick_baselines */
    [[nodiscard]] uint32_t kleeneRoundsScheduled() const noexcept {
        return kleene_rounds_total_;
    }

    /** @brief Executed Kleene squaring rounds for the last preprocess run. @ingroup hbrick_baselines */
    [[nodiscard]] uint32_t kleeneRoundsExecuted() const noexcept {
        return kleene_rounds_executed_;
    }

private:
    enum class PreprocessPhase : uint8_t {
        Idle = 0,
        IndexBuild = 1,
        AdjacencyBuild = 2,
        KleeneClosure = 3,
        Done = 4,
    };

    void resetPreprocessState() noexcept;
    void beginAdjacencyBuild() noexcept;
    void runAdjacencyBatch() noexcept;
    void beginKleeneClosure() noexcept;
    bool runKleeneStep() noexcept;

    BaselineStatus status_ = BaselineStatus::NotRun;
    BrickIndex index_{};
    BitMatrix port_closure_{};
    BitMatrix port_closure_scratch_{};

    BrickIndexBuilder index_builder_{};
    PreprocessPhase preprocess_phase_ = PreprocessPhase::Idle;
    uint64_t preprocess_work_completed_ = 0U;
    uint64_t preprocess_work_total_ = 0U;
    uint32_t adjacency_vertex_cursor_ = 0U;
    uint32_t kleene_rounds_remaining_ = 0U;
    uint32_t kleene_rounds_total_ = 0U;
    uint32_t kleene_rounds_executed_ = 0U;
    bool kleene_scc_compression_tried_ = false;
    KleeneSquaringOptions kleene_options_{};
    uint32_t kleene_thread_count_ = 1U;
    uint32_t num_vertices_ = 0U;
    PreprocessMemoryLedger memory_ledger_{};
    mutable BitVector query_reachable_ports_{};
};

}  // namespace hbrick
