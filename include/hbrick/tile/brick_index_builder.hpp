/**
 * @file brick_index_builder.hpp
 * @ingroup hbrick_tile
 * @brief Incremental flat-BRICK index construction for responsive UIs.
 */

#pragma once

#include <cstdint>
#include <optional>

#include "hbrick/baselines/baseline_status.hpp"
#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/preprocess_memory_ledger.hpp"
#include "hbrick/tile/port_graph.hpp"
#include "hbrick/tile/tile_decomposition.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace hbrick {

class DirectedGridGraph;
class MazeLayout;

/** @brief Active phase of @ref BrickIndexBuilder. @ingroup hbrick_tile */
enum class BrickIndexBuildStage : uint8_t {
    Idle = 0,
    BaseTiles = 1,
    Finalize = 2,
    Finished = 3,
};

/** @brief Live progress for one incremental flat-BRICK build. @ingroup hbrick_tile */
struct BrickIndexBuildProgress {
    BrickIndexBuildStage stage = BrickIndexBuildStage::Idle;
    uint64_t work_completed = 0U;
    uint64_t work_total = 0U;
    uint32_t current_base_tile_index = 0U;
    uint32_t num_base_tiles = 0U;
};

/** @brief Timing summary from a completed @ref BrickIndexBuilder run. @ingroup hbrick_tile */
struct BrickIndexBuildReport {
    bool valid = false;
    BaselineStatus status = BaselineStatus::NotRun;
    uint64_t base_tile_nanoseconds = 0U;
    uint64_t base_tile_closure_nanoseconds = 0U;
    uint64_t finalize_nanoseconds = 0U;
    uint32_t num_base_tiles = 0U;
    uint32_t num_base_completed = 0U;
    uint32_t num_base_skipped = 0U;
    uint32_t num_ports = 0U;
    uint32_t num_seams = 0U;
};

/**
 * @brief Builds a @ref BrickIndex in incremental steps.
 * @ingroup hbrick_tile
 *
 * Performs base-tile closures, port indexing, seam discovery, and port-graph
 * CSR assembly — the same work as @ref BrickIndex::build, but one bounded
 * unit per @ref step for GUI benchmarks.
 */
class BrickIndexBuilder {
public:
    /** @brief Starts a new build; resets prior state. @ingroup hbrick_tile */
    void begin(
        const DirectedGridGraph& graph,
        const MazeLayout& layout,
        TileSize nominal_tile_size,
        uint64_t max_memory_bytes,
        PreprocessMemoryLedger* shared_ledger = nullptr
    );

    /**
     * @brief Advances the build by one work unit.
     * @ingroup hbrick_tile
     * @return @c true when the build is idle, finished, or failed.
     */
    [[nodiscard]] bool step() noexcept;

    /** @brief Aborts an in-flight build. @ingroup hbrick_tile */
    void cancel() noexcept;

    /** @brief Returns whether a build is in progress. @ingroup hbrick_tile */
    [[nodiscard]] bool running() const noexcept;

    /** @brief Live progress snapshot. @ingroup hbrick_tile */
    [[nodiscard]] const BrickIndexBuildProgress& progress() const noexcept;

    /** @brief Report valid after the build finishes. @ingroup hbrick_tile */
    [[nodiscard]] const BrickIndexBuildReport& report() const noexcept;

    /**
     * @brief Moves out the built index after a completed run.
     * @ingroup hbrick_tile
     */
    [[nodiscard]] BrickIndex takeIndex();

    /** @brief Bytes charged on the active preprocess ledger. @ingroup hbrick_tile */
    [[nodiscard]] uint64_t chargedStorageBytes() const noexcept;

    /** @brief Active preprocess ledger for this build (never null after @ref begin). @ingroup hbrick_tile */
    [[nodiscard]] PreprocessMemoryLedger& memoryLedger() noexcept { return *ledger_; }

    [[nodiscard]] const PreprocessMemoryLedger& memoryLedger() const noexcept {
        return *ledger_;
    }

private:
    enum class FinalizeSubstep : uint8_t {
        PortIndex = 0,
        SeamEdges = 1,
        PortGraph = 2,
    };

    void finishFailure(BaselineStatus status) noexcept;
    void finishSuccess() noexcept;
    void advanceWork() noexcept;
    void releaseFinalizeStorage() noexcept;

    const DirectedGridGraph* graph_ = nullptr;
    const MazeLayout* layout_ = nullptr;
    uint64_t max_memory_bytes_ = 0U;
    BrickIndex index_{};
    BrickIndexBuildProgress progress_{};
    BrickIndexBuildReport report_{};

    BrickTileIndex tile_index_{};
    TileDecomposition decomposition_{};

    BrickIndexBuildStage phase_ = BrickIndexBuildStage::Idle;
    uint32_t next_base_tile_index_ = 0U;
    uint8_t finalize_substep_ = 0U;
    uint32_t seam_vertex_cursor_ = 0U;
    uint32_t port_graph_tile_cursor_ = 0U;
    std::optional<CsrGraphBuilder> port_graph_builder_{};
    SeamEdgeDeduper seam_deduper_{};
    PreprocessMemoryLedger owned_ledger_{};
    PreprocessMemoryLedger* ledger_ = nullptr;

    uint64_t base_tile_started_ns_ = 0U;
    uint64_t finalize_started_ns_ = 0U;

    BitMatrix base_tile_closure_scratch_{};
    bool cancelled_ = false;
};

}  // namespace hbrick
