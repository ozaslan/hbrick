#include "orientation.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/scc_decomposition.hpp"

namespace hbrick::tools {

namespace {

/**
 * @brief Forward BFS over the CSR graph collecting the reachable set.
 *
 * Tool-side analysis helper (not a library hot path); uses its own visited
 * bitmap so results can outlive the call.
 */
uint32_t bfsReachableSet(
    const CsrGraph& graph,
    const uint32_t source,
    std::vector<uint8_t>& visited,
    std::vector<uint32_t>& queue
) {
    std::fill(visited.begin(), visited.end(), uint8_t{0});
    queue.clear();
    queue.push_back(source);
    visited[source] = 1U;
    uint32_t count = 1U;

    std::size_t head = 0;
    while (head < queue.size()) {
        const uint32_t vertex = queue[head++];
        for (const uint32_t next : graph.outNeighbors(vertex)) {
            if (visited[next] == 0U) {
                visited[next] = 1U;
                queue.push_back(next);
                ++count;
            }
        }
    }
    return count;
}

void hsvToRgb(const float h, const float s, const float v, uint8_t out[3]) {
    const float c = v * s;
    const float hp = h * 6.0F;
    const float x = c * (1.0F - std::fabs(std::fmod(hp, 2.0F) - 1.0F));
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    if (hp < 1.0F)      { r = c; g = x; }
    else if (hp < 2.0F) { r = x; g = c; }
    else if (hp < 3.0F) { g = c; b = x; }
    else if (hp < 4.0F) { g = x; b = c; }
    else if (hp < 5.0F) { r = x; b = c; }
    else                { r = c; b = x; }
    const float m = v - c;
    out[0] = static_cast<uint8_t>((r + m) * 255.0F);
    out[1] = static_cast<uint8_t>((g + m) * 255.0F);
    out[2] = static_cast<uint8_t>((b + m) * 255.0F);
}

}  // namespace

void regenerateGraph(OrientationState& state, const MazeLayout& layout) {
    RandomAsymmetricParams params;
    params.seed = state.seed;
    params.p_one_way = static_cast<long double>(state.p_one_way);
    params.p_bidirectional = static_cast<long double>(state.p_bidirectional);
    params.gradient_angle_degrees = static_cast<double>(state.gradient_angle_degrees);
    params.p_against_gradient = static_cast<long double>(state.p_against_gradient);

    state.graph = DirectedGridGraphBuilder::build(layout, state.mode, params);
    state.generated = true;

    state.scc_valid = false;
    state.num_components = 0;
    state.largest_component = 0;
    state.condensation_edges = 0;
    state.component_of.clear();
    state.component_size_of.clear();
    state.component_sizes.clear();
    state.component_order.clear();
    state.density = {};
    cancelDensityEstimate(state);
    clearProbe(state);
}

void computeScc(OrientationState& state, const MazeLayout& layout) {
    if (!state.generated) {
        return;
    }

    const CsrGraph& graph = state.graph.csrGraph();
    GraphSearchScratch scratch{graph.numVertices()};
    const SccDecomposition decomposition = SccDecomposition::compute(graph, scratch);

    const uint32_t num_vertices = graph.numVertices();
    state.component_of.assign(num_vertices, 0U);
    state.component_size_of.assign(decomposition.numComponents(), 0U);

    for (uint32_t vertex = 0; vertex < num_vertices; ++vertex) {
        const uint32_t component = decomposition.componentOf(vertex);
        state.component_of[vertex] = component;
        if (layout.isPassable(VertexId{vertex})) {
            ++state.component_size_of[component];
        }
    }

    // Populated components sorted by size, keeping their raw ids so the UI
    // can map a histogram bin back to a concrete component.
    std::vector<std::pair<uint32_t, uint32_t>> populated_components;
    uint32_t populated = 0;
    uint32_t largest = 0;
    for (uint32_t component = 0;
         component < state.component_size_of.size(); ++component) {
        const uint32_t size = state.component_size_of[component];
        if (size > 0U) {
            ++populated;
            largest = std::max(largest, size);
            populated_components.emplace_back(size, component);
        }
    }
    std::sort(populated_components.begin(), populated_components.end(),
              std::greater<>());

    state.component_sizes.clear();
    state.component_order.clear();
    state.component_sizes.reserve(populated_components.size());
    state.component_order.reserve(populated_components.size());
    for (const auto& [size, component] : populated_components) {
        state.component_sizes.push_back(size);
        state.component_order.push_back(component);
    }

    // Distinct inter-component arcs: dedupe via sort, no associative containers.
    std::vector<uint64_t> cross_edges;
    for (uint32_t vertex = 0; vertex < num_vertices; ++vertex) {
        const uint32_t from_component = state.component_of[vertex];
        for (const uint32_t next : graph.outNeighbors(vertex)) {
            const uint32_t to_component = state.component_of[next];
            if (from_component != to_component) {
                cross_edges.push_back(
                    (static_cast<uint64_t>(from_component) << 32U) | to_component
                );
            }
        }
    }
    std::sort(cross_edges.begin(), cross_edges.end());
    cross_edges.erase(
        std::unique(cross_edges.begin(), cross_edges.end()),
        cross_edges.end()
    );

    state.num_components = populated;
    state.largest_component = largest;
    state.condensation_edges = cross_edges.size();
    state.scc_valid = true;
}

namespace {

void finalizeDensityEstimate(
    OrientationState& state,
    const DensityEstimateJob& job,
    const bool stopped_early
) {
    if (job.completed == 0U) {
        return;
    }

    const double n = static_cast<double>(job.completed);
    const double mean = job.sum / n;
    const double variance = std::max(0.0, job.sum_squares / n - mean * mean);
    const bool exact = job.exhaustive && !stopped_early
        && job.completed >= job.source_count;
    const double std_error = exact ? 0.0 : std::sqrt(variance / n);

    state.density.valid = true;
    state.density.density = static_cast<float>(mean);
    state.density.std_error = static_cast<float>(std_error);
    state.density.samples = job.completed;
}

constexpr uint32_t kAutoMinSamples = 64U;
constexpr uint32_t kAutoCheckInterval = 32U;
constexpr uint32_t kAutoStableRoundsRequired = 3U;

bool metricsStable(
    const double prev_density,
    const double prev_std_error,
    const double density,
    const double std_error
) {
    const double density_tol = std::max(
        1e-6,
        0.001 * std::max(prev_density, density)
    );
    const double sigma_tol = std::max(
        1e-7,
        0.03 * std::max(prev_std_error, std_error)
    );
    return std::abs(density - prev_density) <= density_tol
        && std::abs(std_error - prev_std_error) <= sigma_tol;
}

bool checkAutoConvergence(DensityEstimateJob& job) {
    if (!job.auto_samples || job.completed < kAutoMinSamples) {
        return false;
    }
    if (job.completed % kAutoCheckInterval != 0U) {
        return false;
    }

    const double n = static_cast<double>(job.completed);
    const double mean = job.sum / n;
    const double variance = std::max(0.0, job.sum_squares / n - mean * mean);
    const bool exact = job.exhaustive && job.completed >= job.source_count;
    const double std_error = exact ? 0.0 : std::sqrt(variance / n);
    const double density = mean;

    if (!job.checkpoint_initialized) {
        job.checkpoint_density = static_cast<float>(density);
        job.checkpoint_std_error = static_cast<float>(std_error);
        job.checkpoint_initialized = true;
        return false;
    }

    const double prev_density = static_cast<double>(job.checkpoint_density);
    const double prev_std_error = static_cast<double>(job.checkpoint_std_error);

    if (metricsStable(prev_density, prev_std_error, density, std_error)) {
        ++job.stable_rounds;
    } else {
        job.stable_rounds = 0U;
    }

    job.checkpoint_density = static_cast<float>(density);
    job.checkpoint_std_error = static_cast<float>(std_error);
    return job.stable_rounds >= kAutoStableRoundsRequired;
}

}  // namespace

DensityEstimate snapshotDensityJobEstimate(
    const DensityEstimateJob& job
) noexcept {
    DensityEstimate estimate;
    if (job.completed == 0U) {
        return estimate;
    }

    const double n = static_cast<double>(job.completed);
    const double mean = job.sum / n;
    const double variance = std::max(0.0, job.sum_squares / n - mean * mean);
    const bool exact = job.exhaustive && job.completed >= job.source_count;
    const double std_error = exact ? 0.0 : std::sqrt(variance / n);

    estimate.valid = true;
    estimate.density = static_cast<float>(mean);
    estimate.std_error = static_cast<float>(std_error);
    estimate.samples = job.completed;
    return estimate;
}

void cancelDensityEstimate(OrientationState& state) {
    state.density_job = {};
}

void stopDensityEstimate(OrientationState& state) {
    DensityEstimateJob& job = state.density_job;
    if (!job.active) {
        return;
    }
    if (job.completed > 0U) {
        finalizeDensityEstimate(state, job, true);
    }
    job.active = false;
}

void beginDensityEstimate(
    OrientationState& state,
    const MazeLayout& layout,
    const uint32_t samples
) {
    cancelDensityEstimate(state);
    state.density = {};
    if (!state.generated) {
        return;
    }

    DensityEstimateJob job;
    job.requested_samples = samples;
    job.auto_samples = state.density_auto_samples;

    const CsrGraph& graph = state.graph.csrGraph();
    const uint32_t num_vertices = graph.numVertices();

    job.passable.reserve(layout.passableCount());
    for (uint32_t vertex = 0; vertex < num_vertices; ++vertex) {
        if (layout.isPassable(VertexId{vertex})) {
            job.passable.push_back(vertex);
        }
    }
    if (job.passable.empty()) {
        return;
    }

    job.exhaustive = job.passable.size() <= samples;
    job.source_count = job.exhaustive
        ? static_cast<uint32_t>(job.passable.size())
        : samples;
    job.total_passable = static_cast<double>(job.passable.size());
    job.visited.assign(num_vertices, 0U);
    job.queue.reserve(job.passable.size());
    job.pick = std::uniform_int_distribution<std::size_t>(
        0, job.passable.size() - 1
    );
    job.active = true;
    state.density_job = std::move(job);
}

bool stepDensityEstimate(OrientationState& state, const MazeLayout& layout) {
    (void)layout;
    DensityEstimateJob& job = state.density_job;
    if (!job.active) {
        return true;
    }

    const CsrGraph& graph = state.graph.csrGraph();
    const uint32_t source = job.exhaustive
        ? job.passable[job.completed]
        : job.passable[job.pick(job.rng)];
    const uint32_t reached = bfsReachableSet(
        graph, source, job.visited, job.queue
    );
    const double fraction =
        static_cast<double>(reached) / job.total_passable;
    job.sum += fraction;
    job.sum_squares += fraction * fraction;
    ++job.completed;

    if (checkAutoConvergence(job)) {
        finalizeDensityEstimate(state, job, true);
        job.active = false;
        return true;
    }

    if (job.completed >= job.source_count) {
        finalizeDensityEstimate(state, job, false);
        job.active = false;
        return true;
    }
    return false;
}

void estimateDensity(
    OrientationState& state,
    const MazeLayout& layout,
    const uint32_t samples
) {
    beginDensityEstimate(state, layout, samples);
    while (state.density_job.active) {
        stepDensityEstimate(state, layout);
    }
}

void computeProbe(OrientationState& state, const uint32_t source_vertex) {
    if (!state.generated) {
        return;
    }
    const CsrGraph& graph = state.graph.csrGraph();
    const uint32_t num_vertices = graph.numVertices();

    state.probe_reachable.assign(num_vertices, 0U);
    state.probe_depth.assign(num_vertices, 0U);

    std::vector<uint32_t> queue;
    queue.push_back(source_vertex);
    state.probe_reachable[source_vertex] = 1U;

    uint32_t count = 1U;
    uint32_t max_depth = 0U;
    std::size_t head = 0;
    while (head < queue.size()) {
        const uint32_t vertex = queue[head++];
        const uint32_t next_depth = state.probe_depth[vertex] + 1U;
        for (const uint32_t next : graph.outNeighbors(vertex)) {
            if (state.probe_reachable[next] == 0U) {
                state.probe_reachable[next] = 1U;
                state.probe_depth[next] = next_depth;
                max_depth = std::max(max_depth, next_depth);
                queue.push_back(next);
                ++count;
            }
        }
    }

    state.probe_reach_count = count;
    state.probe_max_depth = max_depth;
    state.probe_vertex = source_vertex;
    state.probe_result_mode = ProbeMode::Reachability;
    state.probe_valid = true;
}

void computeComponentProbe(OrientationState& state, const uint32_t source_vertex) {
    if (!state.generated || !state.scc_valid) {
        return;
    }

    const uint32_t num_vertices = state.graph.numVertices();
    state.probe_reachable.assign(num_vertices, 0U);
    state.probe_depth.clear();

    const uint32_t component = state.component_of[source_vertex];
    uint32_t count = 0U;
    for (uint32_t vertex = 0; vertex < num_vertices; ++vertex) {
        if (state.component_of[vertex] == component) {
            state.probe_reachable[vertex] = 1U;
            ++count;
        }
    }

    state.probe_reach_count = count;
    state.probe_max_depth = 0U;
    state.probe_vertex = source_vertex;
    state.probe_result_mode = ProbeMode::Component;
    state.probe_valid = true;
}

void computeComponentProbeById(OrientationState& state, const uint32_t component) {
    if (!state.generated || !state.scc_valid) {
        return;
    }

    const uint32_t num_vertices = state.graph.numVertices();
    state.probe_reachable.assign(num_vertices, 0U);
    state.probe_depth.clear();

    uint32_t count = 0U;
    uint32_t first_member = 0U;
    for (uint32_t vertex = 0; vertex < num_vertices; ++vertex) {
        if (state.component_of[vertex] == component) {
            state.probe_reachable[vertex] = 1U;
            if (count == 0U) {
                first_member = vertex;
            }
            ++count;
        }
    }
    if (count == 0U) {
        return;
    }

    state.probe_reach_count = count;
    state.probe_max_depth = 0U;
    state.probe_vertex = first_member;
    state.probe_result_mode = ProbeMode::Component;
    state.probe_valid = true;
}

void clearProbe(OrientationState& state) {
    state.probe_valid = false;
    state.probe_vertex = 0;
    state.probe_reach_count = 0;
    state.probe_max_depth = 0;
    state.probe_reachable.clear();
    state.probe_depth.clear();
    destroyTexture(state.probe_overlay);
}

void destroyOrientationTextures(OrientationState& state) {
    destroyTexture(state.probe_overlay);
}

void componentColor(const uint32_t component, uint8_t out[4]) {
    // Golden-ratio hue walk: neighboring ids land far apart on the wheel.
    const float hue = std::fmod(
        static_cast<float>(component) * 0.61803398875F, 1.0F
    );
    const float saturation = 0.62F + 0.18F * static_cast<float>((component * 7U) % 3U) * 0.5F;
    const float value = 0.80F + 0.15F * static_cast<float>((component * 13U) % 2U);
    hsvToRgb(hue, saturation, std::min(value, 0.95F), out);
    out[3] = 255U;
}

std::vector<uint8_t> renderSccPixels(
    const MazeLayout& layout,
    const OrientationState& state
) {
    const uint32_t width = layout.width();
    const uint32_t height = layout.height();
    std::vector<uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4U, 0U
    );

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width + x) * 4U;
            uint8_t* pixel = &pixels[offset];
            if (!layout.isPassable(x, y)) {
                pixel[0] = 24U;
                pixel[1] = 24U;
                pixel[2] = 28U;
                pixel[3] = 255U;
                continue;
            }
            const uint32_t vertex = layout.vertexId(GridCoord{x, y}).value;
            componentColor(state.component_of[vertex], pixel);
        }
    }
    return pixels;
}

namespace {

/**
 * @brief Smooth yellow -> orange -> violet ramp for normalized distance.
 *
 * Plasma-like: bright warm tones near the source, cool dark tones at the
 * frontier, readable on both light terrain and dark walls.
 */
void heatColor(const float t, uint8_t out[3]) {
    constexpr float kStops[3][3] = {
        {255.0F, 235.0F, 110.0F},  // near: warm yellow
        {255.0F, 110.0F, 60.0F},   // mid: orange-red
        {165.0F, 45.0F, 235.0F},   // far: violet
    };
    const float clamped = std::clamp(t, 0.0F, 1.0F);
    const std::size_t low = clamped < 0.5F ? 0U : 1U;
    const float local = (clamped - 0.5F * static_cast<float>(low)) * 2.0F;
    for (std::size_t channel = 0; channel < 3U; ++channel) {
        const float value = kStops[low][channel]
            + (kStops[low + 1U][channel] - kStops[low][channel]) * local;
        out[channel] = static_cast<uint8_t>(value);
    }
}

}  // namespace

std::vector<uint8_t> renderProbeOverlayPixels(
    const MazeLayout& layout,
    const OrientationState& state
) {
    const uint32_t width = layout.width();
    const uint32_t height = layout.height();
    std::vector<uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4U, 0U
    );

    const bool by_distance =
        state.probe_result_mode == ProbeMode::Reachability
        && !state.probe_depth.empty();
    const float depth_scale = state.probe_max_depth > 0U
        ? 1.0F / static_cast<float>(state.probe_max_depth)
        : 0.0F;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width + x) * 4U;
            uint8_t* pixel = &pixels[offset];
            if (!layout.isPassable(x, y)) {
                continue;
            }
            const uint32_t vertex = layout.vertexId(GridCoord{x, y}).value;
            if (vertex == state.probe_vertex) {
                pixel[0] = 255U;
                pixel[1] = 255U;
                pixel[2] = 255U;
                pixel[3] = 245U;
            } else if (state.probe_reachable[vertex] != 0U) {
                if (by_distance) {
                    const float t =
                        static_cast<float>(state.probe_depth[vertex]) * depth_scale;
                    heatColor(t, pixel);
                } else {
                    // Component probe: one solid highlight color.
                    pixel[0] = 60U;
                    pixel[1] = 220U;
                    pixel[2] = 255U;
                }
                pixel[3] = 205U;
            } else {
                // Push everything outside the set far into the background.
                pixel[0] = 10U;
                pixel[1] = 10U;
                pixel[2] = 14U;
                pixel[3] = 200U;
            }
        }
    }
    return pixels;
}

}  // namespace hbrick::tools
