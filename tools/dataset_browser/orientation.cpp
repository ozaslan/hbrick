#include "orientation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/scc_decomposition.hpp"

namespace hbrick::tools {

namespace {

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
    cancelReachabilityBenchmark(state);
    clearProbe(state);
}

namespace {

[[nodiscard]] std::vector<uint32_t> passableVertices(const MazeLayout& layout) {
    std::vector<uint32_t> passable;
    passable.reserve(layout.passableCount());
    const uint32_t num_vertices = layout.numVertices();
    for (uint32_t vertex = 0; vertex < num_vertices; ++vertex) {
        if (layout.isPassable(VertexId{vertex})) {
            passable.push_back(vertex);
        }
    }
    return passable;
}

[[nodiscard]] ReachabilityDensityConfig densityConfig(
    const OrientationState& state,
    const uint32_t samples
) {
    ReachabilityDensityConfig config;
    config.max_samples = samples;
    config.sample_mode = state.density_sample_mode;
    config.num_threads = state.density_use_parallel ? 0U : 1U;
    return config;
}

}  // namespace

void cancelDensityEstimate(OrientationState& state) {
    state.density_estimator.cancel();
    state.density_modal_requested = false;
}

void stopDensityEstimate(OrientationState& state) {
    state.density_estimator.stop();
    state.density = state.density_estimator.result();
}

void cancelReachabilityBenchmark(OrientationState& state) {
    state.benchmark_stop.store(true, std::memory_order_release);
    if (state.benchmark_job != nullptr) {
        state.benchmark_job->cancel();
    }

    if (!state.benchmark_timer_frozen
        && state.benchmark_started_at.time_since_epoch().count() != 0) {
        state.benchmark_ended_at = std::chrono::steady_clock::now();
        state.benchmark_timer_frozen = true;
    }

    if (state.benchmark_worker != nullptr && state.benchmark_worker->joinable()) {
        state.benchmark_worker->detach();
    }
    state.benchmark_worker.reset();
    state.benchmark_worker_running.store(false, std::memory_order_release);
}

void reapBenchmarkWorker(OrientationState& state) {
    if (state.benchmark_worker == nullptr) {
        return;
    }

    if (state.benchmark_worker_running.load(std::memory_order_acquire)) {
        return;
    }

    if (!state.benchmark_timer_frozen
        && state.benchmark_started_at.time_since_epoch().count() != 0) {
        state.benchmark_ended_at = std::chrono::steady_clock::now();
        state.benchmark_timer_frozen = true;
    }

    if (state.benchmark_worker->joinable()) {
        state.benchmark_worker->join();
    }
    state.benchmark_worker.reset();
}

bool benchmarkWorkerRunning(const OrientationState& state) noexcept {
    return state.benchmark_worker_running.load(std::memory_order_acquire);
}

namespace {

[[nodiscard]] uint32_t chooseBenchmarkQueriesPerStep(const uint32_t query_count) noexcept {
    if (query_count >= 1'000'000U) {
        return 4096U;
    }
    if (query_count >= 100'000U) {
        return 512U;
    }
    if (query_count >= 10'000U) {
        return 128U;
    }
    return 16U;
}

}  // namespace

void beginReachabilityBenchmark(OrientationState& state, const MazeLayout& layout) {
    cancelReachabilityBenchmark(state);
    reapBenchmarkWorker(state);
    if (benchmarkWorkerRunning(state)) {
        return;
    }

    state.benchmark_stop.store(false, std::memory_order_release);
    if (!state.generated) {
        return;
    }

    const std::vector<uint32_t> universe = passableVertices(layout);
    if (universe.size() < 2U) {
        return;
    }

    state.benchmark_config.pair_seed = state.seed;
    state.benchmark_config.max_memory_bytes = static_cast<uint64_t>(
        static_cast<double>(state.benchmark_memory_gib) * (1024.0 * 1024.0 * 1024.0)
    );
    state.benchmark_config.queries_per_step =
        chooseBenchmarkQueriesPerStep(state.benchmark_config.query_count);

    state.benchmark_job = std::make_shared<hbrick::ReachabilityBenchmarkJob>();
    state.benchmark_job->begin(
        state.graph.csrGraph(),
        universe,
        state.benchmark_config
    );
    if (!state.benchmark_job->active()) {
        state.benchmark_job.reset();
        return;
    }

    state.benchmark_timer_frozen = false;
    state.benchmark_started_at = std::chrono::steady_clock::now();
    state.benchmark_ended_at = {};
    state.benchmark_followup_density = false;
    state.benchmark_show_modal = true;
    state.benchmark_modal_requested = true;
    state.benchmark_worker_running.store(true, std::memory_order_release);

    const std::shared_ptr<hbrick::ReachabilityBenchmarkJob> job = state.benchmark_job;
    state.benchmark_worker = std::make_unique<std::thread>([job, &state]() {
        while (!state.benchmark_stop.load(std::memory_order_acquire)) {
            if (!job->active()) {
                break;
            }
            if (job->step()) {
                break;
            }
        }
        state.benchmark_worker_running.store(false, std::memory_order_release);
    });
}

bool stepReachabilityBenchmark(OrientationState& state) {
    if (state.benchmark_job == nullptr) {
        return true;
    }
    return state.benchmark_job->step();
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

    const std::vector<uint32_t> passable = passableVertices(layout);
    state.density_estimator.begin(
        state.graph.csrGraph(),
        passable,
        densityConfig(state, samples)
    );
}

uint32_t densitySampleCountFromPanelSettings(const OrientationState& state) noexcept {
    static constexpr uint32_t kDensitySampleCounts[] = {
        128U, 256U, 512U, 1024U, 2048U, 4096U, 8192U, 16384U,
    };
    const int index = std::clamp(
        state.density_sample_index,
        0,
        static_cast<int>(std::size(kDensitySampleCounts) - 1U)
    );
    return kDensitySampleCounts[static_cast<std::size_t>(index)];
}

void beginDensityEstimateFromPanelSettings(
    OrientationState& state,
    const MazeLayout& layout
) {
    beginDensityEstimate(state, layout, densitySampleCountFromPanelSettings(state));
}

bool stepDensityEstimate(OrientationState& state, const MazeLayout& layout) {
    (void)layout;
    if (state.density_estimator.step(state.graph.csrGraph())) {
        state.density = state.density_estimator.result();
        return true;
    }
    return false;
}

bool stepDensityEstimateParallel(OrientationState& state, const MazeLayout& layout) {
    (void)layout;
    const bool finished = state.density_use_parallel
        ? state.density_estimator.stepParallel(state.graph.csrGraph())
        : state.density_estimator.step(state.graph.csrGraph());
    if (finished) {
        state.density = state.density_estimator.result();
    }
    return finished;
}

void estimateDensity(
    OrientationState& state,
    const MazeLayout& layout,
    const uint32_t samples
) {
    if (!state.generated) {
        return;
    }

    const std::vector<uint32_t> passable = passableVertices(layout);
    state.density = state.density_estimator.estimate(
        state.graph.csrGraph(),
        passable,
        densityConfig(state, samples)
    );
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
    cancelReachabilityBenchmark(state);
    state.benchmark_job.reset();
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
