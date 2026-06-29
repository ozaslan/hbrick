/**
 * @file orientation.hpp
 * @brief Directed-orientation editor state and analysis helpers for map panels.
 *
 * Everything here operates on @ref hbrick::MazeLayout and the library's
 * grid-to-graph conversion, so it is independent of which dataset produced
 * the layout.
 */

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/bench/reachability_benchmark.hpp"
#include "hbrick/bench/reachability_benchmark_runner.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/graph/reachability_density.hpp"
#include "hbrick/grid/maze_layout.hpp"

#include "map_render.hpp"

namespace hbrick::tools {

/** @brief Number of selectable @ref hbrick::ReachabilityBaselineId values. */
inline constexpr std::size_t kBenchmarkMethodSlotCount = 10U;

/** @brief What right-clicking a cell does on the canvas. */
enum class ProbeMode : uint8_t {
    /** @brief Forward BFS reachable set, colored by hop distance. */
    Reachability,
    /** @brief All cells in the clicked cell's strongly connected component. */
    Component,
    /**
     * @brief Forward H-BRICK reachable set from one source (requires a built index).
     *
     * Uses @ref HBrickBaseline::query for every passable target cell.
     */
    HBrickReachability,
    /**
     * @brief Two-click H-BRICK pair test: first click sets source, second sets target.
     *
     * Highlights source and target; reachable answers color the target green.
     */
    HBrickPair,
};

/** @brief Per-panel state of the directed-orientation editor. */
struct OrientationState {
    bool editor_open = false;
    bool generated = false;

    GridEdgeConversionMode mode = GridEdgeConversionMode::RandomAsymmetric;
    uint64_t seed = 1;
    float p_one_way = 0.30F;
    float p_bidirectional = 0.10F;
    float gradient_angle_degrees = 45.0F;
    float p_against_gradient = 0.02F;
    bool auto_generate = true;

    /** @brief Incremented on each successful graph regeneration; invalidates BRICK index. */
    uint64_t graph_epoch = 0U;
    /** @brief One-shot signal consumed by the map panel loop to auto-open the BRICK panel. */
    bool request_brick_panel_open = false;

    DirectedGridGraph graph;

    bool scc_valid = false;
    uint32_t num_components = 0;       // components containing >= 1 passable cell
    uint32_t largest_component = 0;    // passable cells in the largest SCC
    uint64_t condensation_edges = 0;   // distinct inter-component arcs
    std::vector<uint32_t> component_of;        // raw component id per vertex
    std::vector<uint32_t> component_size_of;   // passable size per raw id
    std::vector<uint32_t> component_sizes;     // populated sizes, sorted desc
    std::vector<uint32_t> component_order;     // raw ids parallel to component_sizes

    ReachabilityDensityEstimate density;
    ReachabilityDensityEstimator density_estimator;
    bool density_modal_requested = false;
    /** @brief Density estimate runs inside the benchmark modal after a successful run. */
    bool benchmark_followup_density = false;
    int density_sample_index = 2;  // default 512 in the UI sample-count list
    ReachabilityDensitySampleMode density_sample_mode =
        ReachabilityDensitySampleMode::AutoStopWhenStable;
    bool density_use_parallel = true;

    ReachabilityBenchmarkConfig benchmark_config;
    std::unique_ptr<hbrick::ReachabilityBenchmarkRunner> benchmark_runner;
    bool benchmark_modal_requested = false;
    bool benchmark_show_modal = false;
    int benchmark_query_count_preset = 2;
    uint32_t benchmark_timed_query_count = 4096U;
    int benchmark_correctness_check_preset = 1;  // default 256 in the UI preset list
    float benchmark_memory_gib = 8.0F;
    /** @brief Auto-stop SccDagClosure / FullClosure when projected total speedup vs CsrBfs is too low. */
    bool benchmark_closure_early_stop = false;
    /** @brief Per-method inclusion toggles for the next benchmark run. */
    std::array<bool, kBenchmarkMethodSlotCount> benchmark_method_enabled{};
    bool benchmark_methods_initialized = false;

    ProbeMode probe_mode = ProbeMode::Reachability;
    bool probe_valid = false;
    ProbeMode probe_result_mode = ProbeMode::Reachability;
    uint32_t probe_vertex = 0;
    uint32_t probe_reach_count = 0;
    uint32_t probe_max_depth = 0;
    std::vector<uint8_t> probe_reachable;
    std::vector<uint32_t> probe_depth;  // hop count, valid where probe_reachable
    bool probe_hbrick_pair_has_source = false;
    uint32_t probe_hbrick_pair_source = 0U;
    uint32_t probe_hbrick_pair_target = 0U;
    bool probe_hbrick_pair_answer_valid = false;
    ReachabilityAnswer probe_hbrick_pair_answer = ReachabilityAnswer::Unreachable;
    GlTexture probe_overlay;
};

/** @brief Rebuilds the directed graph from @p layout under the state's parameters. */
void regenerateGraph(OrientationState& state, const MazeLayout& layout);

/**
 * @brief Runs SCC decomposition on the generated graph and fills component data.
 *
 * Component counts and sizes are restricted to passable cells of @p layout,
 * so blocked cells (isolated vertices in the grid graph) do not inflate the
 * statistics.
 */
void computeScc(OrientationState& state, const MazeLayout& layout);

/**
 * @brief Estimates reachable-pair density in one blocking call.
 *
 * Builds the passable-vertex universe from @p layout, configures
 * @ref ReachabilityDensityEstimator from @p state (sample mode, parallel
 * flag), and runs until completion. Writes the final estimate to
 * @c state.density. No-op when @c state.generated is @c false.
 *
 * Density is the average fraction of passable vertices reachable from
 * distinct passable sources (self included), an unbiased estimate of the
 * ordered reachable-pair fraction.
 *
 * @param state Panel state holding the directed graph and estimator settings.
 * @param layout Maze layout used to enumerate passable vertices.
 * @param samples Maximum distinct BFS sources (@c max_samples).
 */
void estimateDensity(OrientationState& state, const MazeLayout& layout, uint32_t samples);

/**
 * @brief Starts an incremental density estimate for modal progress UI.
 *
 * Cancels any prior job, clears @c state.density, and calls
 * @ref ReachabilityDensityEstimator::begin with passable vertices and
 * @c densityConfig derived from @p state. No-op when @c state.generated is
 * @c false.
 *
 * @param state Panel state holding the directed graph and estimator settings.
 * @param layout Maze layout used to enumerate passable vertices.
 * @param samples Maximum distinct BFS sources (@c max_samples).
 */
void beginDensityEstimate(
    OrientationState& state,
    const MazeLayout& layout,
    uint32_t samples
);

/** @brief Returns the max-sample count selected in the orientation panel UI. */
[[nodiscard]] uint32_t densitySampleCountFromPanelSettings(
    const OrientationState& state
) noexcept;

/** @brief Starts a density job using orientation-panel sample settings. */
void beginDensityEstimateFromPanelSettings(
    OrientationState& state,
    const MazeLayout& layout
);

/**
 * @brief Runs one serial BFS sample of an active density job.
 *
 * Delegates to @ref ReachabilityDensityEstimator::step. On completion,
 * copies @ref ReachabilityDensityEstimator::result into @c state.density.
 *
 * @param state Panel state holding the active estimator.
 * @param layout Unused; kept for a uniform stepping API with layout-aware helpers.
 * @return @c true when the job finished and @c state.density was updated;
 *         @c false when more samples remain.
 */
bool stepDensityEstimate(OrientationState& state, const MazeLayout& layout);

/**
 * @brief Runs a batch of BFS samples for an active density job.
 *
 * When @c state.density_use_parallel is @c true, calls
 * @ref ReachabilityDensityEstimator::stepParallel (hardware thread count,
 * distinct pre-shuffled sources per worker). Otherwise runs a single
 * @ref ReachabilityDensityEstimator::step. On completion, copies the
 * result into @c state.density.
 *
 * @param state Panel state holding the active estimator and parallel flag.
 * @param layout Unused; kept for a uniform stepping API with layout-aware helpers.
 * @return @c true when the job finished and @c state.density was updated;
 *         @c false when more samples remain.
 */
bool stepDensityEstimateParallel(OrientationState& state, const MazeLayout& layout);

/**
 * @brief Aborts an in-progress density job without updating the result.
 *
 * Calls @ref ReachabilityDensityEstimator::cancel and clears
 * @c state.density_modal_requested.
 *
 * @param state Panel state holding the active estimator.
 */
void cancelDensityEstimate(OrientationState& state);

/**
 * @brief Stops an active job and keeps the estimate from completed samples.
 *
 * Calls @ref ReachabilityDensityEstimator::stop and copies
 * @ref ReachabilityDensityEstimator::result into @c state.density.
 * No-op when the job is inactive or no samples have finished.
 *
 * @param state Panel state holding the active estimator.
 */
void stopDensityEstimate(OrientationState& state);

/**
 * @brief Opens the benchmark results modal without starting a job.
 */
void openBenchmarkPanel(OrientationState& state) noexcept;

/**
 * @brief Ensures @ref OrientationState::benchmark_method_enabled has default selections.
 */
void ensureBenchmarkMethodDefaults(OrientationState& state) noexcept;

/**
 * @brief Resets method checkboxes to the library default benchmark set.
 */
void resetBenchmarkMethodDefaults(OrientationState& state) noexcept;

/** @brief Returns how many benchmark methods are currently selected. */
[[nodiscard]] std::size_t countSelectedBenchmarkMethods(
    const OrientationState& state
) noexcept;

/**
 * @brief Builds @ref hbrick::ReachabilityBenchmarkConfig from panel fields and selected methods.
 *
 * @return @c false when no methods are selected.
 */
[[nodiscard]] bool prepareBenchmarkConfigFromPanel(OrientationState& state) noexcept;

/**
 * @brief Starts an incremental reachability baseline benchmark.
 *
 * Uses passable maze vertices as the query universe and the panel's generated
 * directed CSR graph. No-op when @c state.generated is @c false or no methods
 * are selected. Call @ref prepareBenchmarkConfigFromPanel first.
 */
void beginReachabilityBenchmark(OrientationState& state, const MazeLayout& layout);

/** @brief Aborts an active benchmark job without blocking the caller. */
void cancelReachabilityBenchmark(OrientationState& state);

/**
 * @brief Discards the current benchmark runner, results, and follow-up density job.
 *
 * Keeps method selection and workload settings. Safe while idle or after a run.
 */
void resetBenchmarkSession(OrientationState& state) noexcept;

/** @brief Skips the active benchmark method (e.g. aborts closure Warshall preprocessing). */
void skipCurrentBenchmarkMethod(OrientationState& state);

/** @brief Joins a finished benchmark worker thread; safe to call each frame. */
void reapBenchmarkWorker(OrientationState& state);

/**
 * @brief Advances an active benchmark job on the main thread (frame-budgeted).
 *
 * Safe to call every frame while a benchmark modal is open.
 */
void tickReachabilityBenchmark(OrientationState& state) noexcept;

/** @brief Returns @c true while a benchmark job is still running. */
[[nodiscard]] bool benchmarkWorkerRunning(const OrientationState& state) noexcept;

/**
 * @brief Computes the forward-reachable set from @p source_vertex with hop
 *        distances and stores it as the probe overlay flags.
 */
void computeProbe(OrientationState& state, uint32_t source_vertex);

/**
 * @brief Marks passable cells reachable from @p source_vertex via @ref HBrickBaseline.
 *
 * @return @c false when @p baseline is not ready or passable cell count exceeds the GUI limit.
 */
[[nodiscard]] bool computeHBrickReachabilityProbe(
    OrientationState& state,
    const HBrickBaseline& baseline,
    const MazeLayout& layout,
    uint32_t source_vertex
);

/**
 * @brief Advances the two-click H-BRICK pair probe at @p vertex.
 *
 * @return @c true when source and target are set and @p baseline answered the pair.
 */
[[nodiscard]] bool advanceHBrickPairProbe(
    OrientationState& state,
    const HBrickBaseline& baseline,
    uint32_t vertex
);

/** @brief Cycles @p mode through the four probe modes. */
void cycleProbeMode(ProbeMode& mode) noexcept;

/**
 * @brief Marks all cells sharing @p source_vertex's SCC as the probe set.
 *
 * Requires a prior @ref computeScc; does nothing when labels are missing.
 */
void computeComponentProbe(OrientationState& state, uint32_t source_vertex);

/**
 * @brief Marks all cells of the SCC with raw id @p component as the probe set.
 *
 * Used by the histogram bins, which reference components by id rather than by
 * a member cell. Requires a prior @ref computeScc.
 */
void computeComponentProbeById(OrientationState& state, uint32_t component);

/** @brief Clears probe state and its overlay texture. */
void clearProbe(OrientationState& state);

/** @brief Releases GPU resources held by the state. */
void destroyOrientationTextures(OrientationState& state);

/** @brief Distinct stable color for an SCC id (golden-ratio hue hash). */
void componentColor(uint32_t component, uint8_t out[4]);

/** @brief Row-major RGBA pixels coloring passable cells by SCC id. */
[[nodiscard]] std::vector<uint8_t> renderSccPixels(
    const MazeLayout& layout,
    const OrientationState& state
);

/** @brief Row-major RGBA overlay pixels for the probe's reachable set. */
[[nodiscard]] std::vector<uint8_t> renderProbeOverlayPixels(
    const MazeLayout& layout,
    const OrientationState& state
);

}  // namespace hbrick::tools
