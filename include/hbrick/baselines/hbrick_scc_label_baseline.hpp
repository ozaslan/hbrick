/**
 * @file hbrick_scc_label_baseline.hpp
 * @ingroup hbrick_baselines
 * @brief H-BRICK reachability baseline with SCC-condensed materialized ancestor labels.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/bit/bit_vector.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hbrick_index.hpp"

namespace hbrick {

/**
 * @brief Condensed local SCC metadata for one base tile.
 * @ingroup hbrick_baselines
 */
struct TileSccPartition {
    uint32_t num_sccs = 0U;
    /** @brief Maps local cell index in base tile -> local SCC index. */
    std::vector<uint32_t> cell_to_scc;
};

/**
 * @brief Materialized ancestor reachability labels for one local SCC in a base tile.
 * @ingroup hbrick_baselines
 */
struct SccAncestorLabel {
    /** @brief Forward label in Gamma_U space: ports reachable in ancestor U. */
    BitVector forward_gamma;
    /** @brief Reverse label in Gamma_U space: ports that can reach into this SCC in ancestor U. */
    BitVector reverse_gamma;
};

/**
 * @brief All SCC-keyed ancestor labels for one base tile.
 * @ingroup hbrick_baselines
 */
struct TileSccLabels {
    TileSccPartition partition;
    /** @brief labels[scc_index][ancestor_level_index] */
    std::vector<std::vector<SccAncestorLabel>> labels;
};

/**
 * @brief H-BRICK reachability baseline with SCC-keyed materialized ancestor labels.
 * @ingroup hbrick_baselines
 *
 * Avoids cell-level O(n_T * gamma) label storage explosion by indexing labels
 * by local Strongly Connected Components (SCCs).
 */
class HBrickSccLabelBaseline {
public:
    void preprocess(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        HBrickConfig config
    );

    void adoptPrebuiltIndex(HBrickIndex index, const DirectedGridGraph* graph = nullptr);

    [[nodiscard]] ReachabilityAnswer query(uint32_t source, uint32_t target) const noexcept;

    [[nodiscard]] HBrickQueryOutcome queryDetailed(
        uint32_t source,
        uint32_t target
    ) const noexcept;

    [[nodiscard]] BaselineStatus status() const noexcept { return status_; }
    [[nodiscard]] uint64_t indexStorageBytes() const noexcept;
    [[nodiscard]] uint64_t measuredStorageBytes() const noexcept { return indexStorageBytes(); }
    [[nodiscard]] uint64_t baseIndexStorageBytes() const noexcept { return index_.measureStorageBytes(); }
    [[nodiscard]] uint64_t sccLabelsMemoryBytes() const noexcept;
    [[nodiscard]] uint64_t scratchMemoryBytes() const noexcept;
    [[nodiscard]] const HBrickIndex& index() const noexcept { return index_; }
    [[nodiscard]] GraphSearchScratch& portBfsScratch() noexcept { return port_bfs_scratch_; }
    [[nodiscard]] const GraphSearchScratch& portBfsScratch() const noexcept { return port_bfs_scratch_; }
    [[nodiscard]] GraphSearchScratch& microBfsScratch() noexcept { return micro_bfs_scratch_; }
    [[nodiscard]] const GraphSearchScratch& microBfsScratch() const noexcept { return micro_bfs_scratch_; }

private:
    void materializeSccLabels();

    BaselineStatus status_ = BaselineStatus::NotRun;
    HBrickIndex index_{};
    const DirectedGridGraph* graph_ = nullptr;
    std::vector<TileSccLabels> tile_labels_{};
    mutable GraphSearchScratch port_bfs_scratch_{};
    mutable GraphSearchScratch micro_bfs_scratch_{};
};

}  // namespace hbrick
