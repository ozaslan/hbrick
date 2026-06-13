/**
 * @file orientation.hpp
 * @brief Directed-orientation editor state and analysis helpers for map panels.
 *
 * Everything here operates on @ref hbrick::MazeLayout and the library's
 * grid-to-graph conversion, so it is independent of which dataset produced
 * the layout.
 */

#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"

#include "map_render.hpp"

namespace hbrick::tools {

/** @brief What right-clicking a cell highlights on the canvas. */
enum class ProbeMode : uint8_t {
    /** @brief Forward BFS reachable set, colored by hop distance. */
    Reachability,
    /** @brief All cells in the clicked cell's strongly connected component. */
    Component,
};

/** @brief Result of a sampled reachable-pair density estimate. */
struct DensityEstimate {
    bool valid = false;
    float density = 0.0F;
    float std_error = 0.0F;
    uint32_t samples = 0;
};

/** @brief Incremental reachable-pair density job (one BFS sample per step). */
struct DensityEstimateJob {
    bool active = false;
    bool modal_requested = false;
    bool auto_samples = false;
    uint32_t requested_samples = 512;
    uint32_t completed = 0;
    uint32_t source_count = 0;
    bool exhaustive = false;
    double total_passable = 0.0;
    double sum = 0.0;
    double sum_squares = 0.0;
    float checkpoint_density = 0.0F;
    float checkpoint_std_error = 0.0F;
    bool checkpoint_initialized = false;
    uint32_t stable_rounds = 0;
    std::vector<uint32_t> passable;
    std::vector<uint8_t> visited;
    std::vector<uint32_t> queue;
    std::mt19937_64 rng{0x9E3779B97F4A7C15ULL};
    std::uniform_int_distribution<std::size_t> pick{0, 0};
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

    DirectedGridGraph graph;

    bool scc_valid = false;
    uint32_t num_components = 0;       // components containing >= 1 passable cell
    uint32_t largest_component = 0;    // passable cells in the largest SCC
    uint64_t condensation_edges = 0;   // distinct inter-component arcs
    std::vector<uint32_t> component_of;        // raw component id per vertex
    std::vector<uint32_t> component_size_of;   // passable size per raw id
    std::vector<uint32_t> component_sizes;     // populated sizes, sorted desc
    std::vector<uint32_t> component_order;     // raw ids parallel to component_sizes

    DensityEstimate density;
    DensityEstimateJob density_job;
    int density_sample_index = 2;  // default 512 in the UI sample-count list
    bool density_auto_samples = true;

    ProbeMode probe_mode = ProbeMode::Reachability;
    bool probe_valid = false;
    ProbeMode probe_result_mode = ProbeMode::Reachability;
    uint32_t probe_vertex = 0;
    uint32_t probe_reach_count = 0;
    uint32_t probe_max_depth = 0;
    std::vector<uint8_t> probe_reachable;
    std::vector<uint32_t> probe_depth;  // hop count, valid where probe_reachable
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
 * @brief Estimates reachable-pair density by forward BFS from sampled sources.
 *
 * Density is the average fraction of passable vertices reachable from a
 * uniformly sampled passable source (self included), an unbiased estimate of
 * the ordered reachable-pair fraction.
 */
void estimateDensity(OrientationState& state, const MazeLayout& layout, uint32_t samples);

/** @brief Starts an incremental density estimate; no-op when the graph is missing. */
void beginDensityEstimate(
    OrientationState& state,
    const MazeLayout& layout,
    uint32_t samples
);

/**
 * @brief Runs one BFS sample of an active job.
 *
 * @return @c true when the job is finished (success or cancelled setup).
 */
bool stepDensityEstimate(OrientationState& state, const MazeLayout& layout);

/** @brief Aborts an in-progress density job without updating the result. */
void cancelDensityEstimate(OrientationState& state);

/**
 * @brief Stops an active job and keeps the estimate from completed samples.
 *
 * No-op when the job is inactive or no samples have finished yet.
 */
void stopDensityEstimate(OrientationState& state);

/** @brief Running mean and standard error from an active or partial job. */
[[nodiscard]] DensityEstimate snapshotDensityJobEstimate(
    const DensityEstimateJob& job
) noexcept;

/**
 * @brief Computes the forward-reachable set from @p source_vertex with hop
 *        distances and stores it as the probe overlay flags.
 */
void computeProbe(OrientationState& state, uint32_t source_vertex);

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
