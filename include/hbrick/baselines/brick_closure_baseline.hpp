/**
 * @file brick_closure_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief Flat BRICK reachability via precomputed port-graph transitive closure.
 */

#pragma once

#include <cstdint>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/**
 * @brief BRICK-Closure baseline: truncated Kleene closure on the global port graph + attachments.
 * @ingroup hbrick_baselines
 *
 * Preprocessing builds a @ref BrickIndex (base tiles + global port CSR) and then
 * materializes a reflexive transitive closure over the port graph via Kleene squaring
 * (@c ceil(log2(n_max)) rounds on the largest undirected port component).
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
        uint64_t max_memory_bytes
    );

    /**
     * @brief Answers reachability using BRICK-Closure (no port BFS at query time).
     * @ingroup hbrick_baselines
     *
     * @param source Source vertex in the global grid graph.
     * @param target Target vertex in the global grid graph.
     */
    [[nodiscard]] ReachabilityAnswer query(
        uint32_t source,
        uint32_t target
    ) const noexcept;

    /** @brief Returns the outcome of the most recent @ref preprocess call. @ingroup hbrick_baselines */
    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }

    /** @brief Returns the built index when preprocessing completed. @ingroup hbrick_baselines */
    [[nodiscard]] const BrickIndex& index() const noexcept { return index_; }

    /**
     * @brief Returns port-closure matrix storage bytes after successful preprocessing.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept;

    /**
     * @brief Returns the precomputed port-graph closure after successful preprocessing.
     * @ingroup hbrick_baselines
     */
    [[nodiscard]] const BitMatrix& portClosure() const noexcept { return port_closure_; }

private:
    BaselineStatus status_ = BaselineStatus::NotRun;
    BrickIndex index_{};
    BitMatrix port_closure_{};
    BitMatrix port_closure_scratch_{};
};

}  // namespace hbrick

