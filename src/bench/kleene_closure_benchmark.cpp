/**
 * @file kleene_closure_benchmark.cpp
 * @brief Thorough Kleene squaring vs Warshall closure timing on varied directed graphs.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <random>
#include <vector>

#include "hbrick/bench/bench_timer.hpp"
#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/connected_components.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/tile/tile_closure_util.hpp"

namespace {

using hbrick::BenchTimer;
using hbrick::BitMatrix;
using hbrick::BooleanClosure;
using hbrick::CsrGraph;
using hbrick::CsrGraphBuilder;
using hbrick::buildTileReflexiveAdjacencyOrThrow;
using hbrick::largestUndirectedComponentSize;

struct SampleStats {
    uint64_t p50 = 0U;
    uint64_t p10 = 0U;
    uint64_t p90 = 0U;
};

[[nodiscard]] SampleStats computeStats(std::vector<uint64_t> samples) {
    SampleStats stats{};
    if (samples.empty()) {
        return stats;
    }
    std::sort(samples.begin(), samples.end());
    const auto at = [&](const double quantile) -> uint64_t {
        const std::size_t index = static_cast<std::size_t>(
            quantile * static_cast<double>(samples.size() - 1U)
        );
        return samples[index];
    };
    stats.p10 = at(0.10);
    stats.p50 = at(0.50);
    stats.p90 = at(0.90);
    return stats;
}

[[nodiscard]] uint32_t fullSquaringCount(const uint32_t num_vertices) noexcept {
    return BooleanClosure::kleeneSquaringCountForLargestComponent(num_vertices);
}

[[nodiscard]] CsrGraph makeRandomDirectedGraph(
    const uint32_t num_vertices,
    const double edge_probability,
    std::mt19937_64& rng
) {
    CsrGraphBuilder builder{num_vertices};
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    for (uint32_t from = 0U; from < num_vertices; ++from) {
        for (uint32_t to = 0U; to < num_vertices; ++to) {
            if (from == to) {
                continue;
            }
            if (coin(rng) < edge_probability) {
                builder.addEdge(from, to);
            }
        }
    }
    return builder.build();
}

[[nodiscard]] CsrGraph makeDisconnectedRandomGraph(
    const uint32_t num_vertices,
    const uint32_t num_components,
    const double edge_probability,
    std::mt19937_64& rng
) {
    CsrGraphBuilder builder{num_vertices};
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    const uint32_t safe_components = std::max(1U, num_components);
    const uint32_t base_size = num_vertices / safe_components;
    const uint32_t remainder = num_vertices % safe_components;
    uint32_t offset = 0U;
    for (uint32_t component = 0U; component < safe_components; ++component) {
        const uint32_t component_size =
            base_size + (component < remainder ? 1U : 0U);
        const uint32_t end = offset + component_size;
        for (uint32_t local_from = offset; local_from < end; ++local_from) {
            for (uint32_t local_to = offset; local_to < end; ++local_to) {
                if (local_from == local_to) {
                    continue;
                }
                if (coin(rng) < edge_probability) {
                    builder.addEdge(local_from, local_to);
                }
            }
        }
        offset = end;
    }
    return builder.build();
}

[[nodiscard]] CsrGraph makeDirectedChain(const uint32_t num_vertices) {
    CsrGraphBuilder builder{num_vertices};
    for (uint32_t vertex = 0U; vertex + 1U < num_vertices; ++vertex) {
        builder.addEdge(vertex, vertex + 1U);
    }
    return builder.build();
}

[[nodiscard]] CsrGraph makeDirectedCycle(const uint32_t num_vertices) {
    CsrGraphBuilder builder{num_vertices};
    for (uint32_t vertex = 0U; vertex < num_vertices; ++vertex) {
        builder.addEdge(vertex, (vertex + 1U) % num_vertices);
    }
    return builder.build();
}

[[nodiscard]] CsrGraph makeCompleteDirected(const uint32_t num_vertices) {
    CsrGraphBuilder builder{num_vertices};
    for (uint32_t from = 0U; from < num_vertices; ++from) {
        for (uint32_t to = 0U; to < num_vertices; ++to) {
            if (from != to) {
                builder.addEdge(from, to);
            }
        }
    }
    return builder.build();
}

[[nodiscard]] CsrGraph makeStarOut(const uint32_t num_vertices) {
    CsrGraphBuilder builder{num_vertices};
    for (uint32_t leaf = 1U; leaf < num_vertices; ++leaf) {
        builder.addEdge(0U, leaf);
    }
    return builder.build();
}

[[nodiscard]] CsrGraph makeStarIn(const uint32_t num_vertices) {
    CsrGraphBuilder builder{num_vertices};
    for (uint32_t leaf = 1U; leaf < num_vertices; ++leaf) {
        builder.addEdge(leaf, 0U);
    }
    return builder.build();
}

[[nodiscard]] CsrGraph makeBipartiteRandom(
    const uint32_t num_vertices,
    const double edge_probability,
    std::mt19937_64& rng
) {
    CsrGraphBuilder builder{num_vertices};
    const uint32_t split = num_vertices / 2U;
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    for (uint32_t left = 0U; left < split; ++left) {
        for (uint32_t right = split; right < num_vertices; ++right) {
            if (coin(rng) < edge_probability) {
                builder.addEdge(left, right);
            }
        }
    }
    return builder.build();
}

[[nodiscard]] CsrGraph makeGridRandomOrientation(
    const uint32_t width,
    const uint32_t height,
    const double wall_probability,
    std::mt19937_64& rng
) {
    const uint32_t cell_count = width * height;
    std::vector<uint32_t> vertex_id(cell_count, std::numeric_limits<uint32_t>::max());
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    std::uniform_int_distribution<int> direction_coin(0, 1);

    uint32_t next_vertex = 0U;
    for (uint32_t y = 0U; y < height; ++y) {
        for (uint32_t x = 0U; x < width; ++x) {
            const uint32_t cell_index = y * width + x;
            if (coin(rng) >= wall_probability) {
                vertex_id[cell_index] = next_vertex++;
            }
        }
    }

    CsrGraphBuilder builder{next_vertex};
    auto add_adjacency = [&](const uint32_t from_cell, const uint32_t to_cell) {
        const uint32_t from = vertex_id[from_cell];
        const uint32_t to = vertex_id[to_cell];
        if (from == std::numeric_limits<uint32_t>::max()
            || to == std::numeric_limits<uint32_t>::max()) {
            return;
        }
        if (direction_coin(rng) == 0) {
            builder.addEdge(from, to);
        } else {
            builder.addEdge(to, from);
        }
    };

    for (uint32_t y = 0U; y < height; ++y) {
        for (uint32_t x = 0U; x < width; ++x) {
            const uint32_t cell_index = y * width + x;
            if (vertex_id[cell_index] == std::numeric_limits<uint32_t>::max()) {
                continue;
            }
            if (x + 1U < width) {
                add_adjacency(cell_index, y * width + (x + 1U));
            }
            if (y + 1U < height) {
                add_adjacency(cell_index, (y + 1U) * width + x);
            }
        }
    }
    return builder.build();
}

[[nodiscard]] CsrGraph makeGridEastSouthDag(
    const uint32_t width,
    const uint32_t height,
    const double wall_probability,
    std::mt19937_64& rng
) {
    const uint32_t cell_count = width * height;
    std::vector<uint32_t> vertex_id(cell_count, std::numeric_limits<uint32_t>::max());
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    uint32_t next_vertex = 0U;
    for (uint32_t y = 0U; y < height; ++y) {
        for (uint32_t x = 0U; x < width; ++x) {
            const uint32_t cell_index = y * width + x;
            if (coin(rng) >= wall_probability) {
                vertex_id[cell_index] = next_vertex++;
            }
        }
    }

    CsrGraphBuilder builder{next_vertex};
    auto add_if_passable = [&](const uint32_t from_cell, const uint32_t to_cell) {
        const uint32_t from = vertex_id[from_cell];
        const uint32_t to = vertex_id[to_cell];
        if (from != std::numeric_limits<uint32_t>::max()
            && to != std::numeric_limits<uint32_t>::max()) {
            builder.addEdge(from, to);
        }
    };

    for (uint32_t y = 0U; y < height; ++y) {
        for (uint32_t x = 0U; x < width; ++x) {
            const uint32_t cell_index = y * width + x;
            if (vertex_id[cell_index] == std::numeric_limits<uint32_t>::max()) {
                continue;
            }
            if (x + 1U < width) {
                add_if_passable(cell_index, y * width + (x + 1U));
            }
            if (y + 1U < height) {
                add_if_passable(cell_index, (y + 1U) * width + x);
            }
        }
    }
    return builder.build();
}

struct TimingResult {
    uint32_t num_vertices = 0U;
    uint64_t num_edges = 0U;
    double edge_density = 0.0;
    uint32_t largest_component = 0U;
    uint32_t squaring_count_trunc = 0U;
    uint32_t squaring_count_full = 0U;
    bool verify_trunc_ok = true;
    bool verify_full_ok = true;
    SampleStats cc{};
    SampleStats warshall{};
    SampleStats kleene_trunc{};
    SampleStats kleene_full{};
};

struct ClosureVerifyResult {
    bool trunc_matches = false;
    bool full_matches = false;
};

struct VerifyCounters {
    uint32_t graphs_checked = 0U;
    uint32_t trunc_mismatches = 0U;
    uint32_t full_mismatches = 0U;
};

VerifyCounters g_verify_counters{};

[[nodiscard]] ClosureVerifyResult verifyClosuresFromBase(
    const BitMatrix& base,
    const CsrGraph& graph
) {
    const uint32_t num_vertices = graph.numVertices();
    if (num_vertices == 0U) {
        return {true, true};
    }

    const uint32_t largest_component =
        largestUndirectedComponentSize(graph);
    const uint32_t squaring_count_trunc =
        BooleanClosure::kleeneSquaringCountForLargestComponent(largest_component);
    const uint32_t squaring_count_full = fullSquaringCount(num_vertices);

    BitMatrix warshall = base;
    BooleanClosure::transitiveClosureWarshallInPlace(warshall);

    BitMatrix kleene_trunc = base;
    BooleanClosure::transitiveClosureKleeneSquaringInPlace(
        kleene_trunc,
        squaring_count_trunc
    );

    BitMatrix kleene_full = base;
    BooleanClosure::transitiveClosureKleeneSquaringInPlace(
        kleene_full,
        squaring_count_full
    );

    return {
        bitMatricesEqual(warshall, kleene_trunc),
        bitMatricesEqual(warshall, kleene_full),
    };
}

void recordVerification(
    const char* family,
    const CsrGraph& graph,
    const ClosureVerifyResult& verify
) {
    ++g_verify_counters.graphs_checked;
    if (!verify.trunc_matches) {
        ++g_verify_counters.trunc_mismatches;
        std::fprintf(
            stderr,
            "VERIFY FAIL (k_trunc): family=%s M=%u edges=%llu n_max=%u\n",
            family,
            graph.numVertices(),
            static_cast<unsigned long long>(graph.numEdges()),
            largestUndirectedComponentSize(graph)
        );
    }
    if (!verify.full_matches) {
        ++g_verify_counters.full_mismatches;
        std::fprintf(
            stderr,
            "VERIFY FAIL (k_full): family=%s M=%u edges=%llu M=%u\n",
            family,
            graph.numVertices(),
            static_cast<unsigned long long>(graph.numEdges()),
            graph.numVertices()
        );
    }
}

[[nodiscard]] uint32_t warmupIterations(const uint32_t num_vertices) {
    if (num_vertices <= 32U) {
        return 20U;
    }
    if (num_vertices <= 128U) {
        return 10U;
    }
    return 5U;
}

[[nodiscard]] uint32_t measuredIterations(const uint32_t num_vertices) {
    if (num_vertices <= 8U) {
        return 5000U;
    }
    if (num_vertices <= 16U) {
        return 3000U;
    }
    if (num_vertices <= 32U) {
        return 1000U;
    }
    if (num_vertices <= 64U) {
        return 400U;
    }
    if (num_vertices <= 128U) {
        return 150U;
    }
    if (num_vertices <= 192U) {
        return 60U;
    }
    return 25U;
}

struct RawSamples {
    std::vector<uint64_t> cc;
    std::vector<uint64_t> warshall;
    std::vector<uint64_t> kleene_trunc;
    std::vector<uint64_t> kleene_full;
    ClosureVerifyResult verify{};
};

[[nodiscard]] RawSamples measureGraph(
    const CsrGraph& graph,
    const uint32_t warmup_iterations,
    const uint32_t measured_iterations
) {
    const uint64_t max_memory = std::numeric_limits<uint64_t>::max();
    const BitMatrix base = buildTileReflexiveAdjacencyOrThrow(graph, max_memory);

    const uint32_t largest_component = largestUndirectedComponentSize(graph);
    const uint32_t squaring_count_trunc =
        BooleanClosure::kleeneSquaringCountForLargestComponent(largest_component);
    const uint32_t squaring_count_full = fullSquaringCount(graph.numVertices());

    RawSamples samples{};
    samples.verify = verifyClosuresFromBase(base, graph);

    BenchTimer timer{};

    for (uint32_t iteration = 0U; iteration < warmup_iterations; ++iteration) {
        BitMatrix relation = base;
        BooleanClosure::transitiveClosureWarshallInPlace(relation);
    }

    samples.warshall.reserve(measured_iterations);
    for (uint32_t iteration = 0U; iteration < measured_iterations; ++iteration) {
        BitMatrix relation = base;
        timer.start();
        BooleanClosure::transitiveClosureWarshallInPlace(relation);
        timer.stop();
        samples.warshall.push_back(timer.elapsedNanoseconds());
    }

    samples.cc.reserve(measured_iterations);
    for (uint32_t iteration = 0U; iteration < measured_iterations; ++iteration) {
        timer.start();
        (void)largestUndirectedComponentSize(graph);
        timer.stop();
        samples.cc.push_back(timer.elapsedNanoseconds());
    }

    for (uint32_t iteration = 0U; iteration < warmup_iterations; ++iteration) {
        BitMatrix relation = base;
        BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            relation,
            squaring_count_trunc
        );
    }

    samples.kleene_trunc.reserve(measured_iterations);
    for (uint32_t iteration = 0U; iteration < measured_iterations; ++iteration) {
        BitMatrix relation = base;
        timer.start();
        BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            relation,
            squaring_count_trunc
        );
        timer.stop();
        samples.kleene_trunc.push_back(timer.elapsedNanoseconds());
    }

    for (uint32_t iteration = 0U; iteration < warmup_iterations; ++iteration) {
        BitMatrix relation = base;
        BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            relation,
            squaring_count_full
        );
    }

    samples.kleene_full.reserve(measured_iterations);
    for (uint32_t iteration = 0U; iteration < measured_iterations; ++iteration) {
        BitMatrix relation = base;
        timer.start();
        BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            relation,
            squaring_count_full
        );
        timer.stop();
        samples.kleene_full.push_back(timer.elapsedNanoseconds());
    }

    return samples;
}

void appendSamples(RawSamples& dst, const RawSamples& src) {
    dst.cc.insert(dst.cc.end(), src.cc.begin(), src.cc.end());
    dst.warshall.insert(dst.warshall.end(), src.warshall.begin(), src.warshall.end());
    dst.kleene_trunc.insert(
        dst.kleene_trunc.end(),
        src.kleene_trunc.begin(),
        src.kleene_trunc.end()
    );
    dst.kleene_full.insert(
        dst.kleene_full.end(),
        src.kleene_full.begin(),
        src.kleene_full.end()
    );
}

[[nodiscard]] TimingResult benchmarkGraph(
    const char* family,
    const CsrGraph& graph,
    const uint32_t warmup_iterations,
    const uint32_t measured_iterations
) {
    const RawSamples samples = measureGraph(graph, warmup_iterations, measured_iterations);
    recordVerification(family, graph, samples.verify);
    const uint32_t num_vertices = graph.numVertices();

    TimingResult result{};
    result.num_vertices = num_vertices;
    result.num_edges = graph.numEdges();
    if (num_vertices > 1U) {
        const double denom =
            static_cast<double>(num_vertices) * static_cast<double>(num_vertices - 1U);
        result.edge_density = static_cast<double>(result.num_edges) / denom;
    }
    result.largest_component = largestUndirectedComponentSize(graph);
    result.squaring_count_trunc =
        BooleanClosure::kleeneSquaringCountForLargestComponent(result.largest_component);
    result.squaring_count_full = fullSquaringCount(num_vertices);
    result.verify_trunc_ok = samples.verify.trunc_matches;
    result.verify_full_ok = samples.verify.full_matches;
    result.cc = computeStats(samples.cc);
    result.warshall = computeStats(samples.warshall);
    result.kleene_trunc = computeStats(samples.kleene_trunc);
    result.kleene_full = computeStats(samples.kleene_full);
    return result;
}

[[nodiscard]] TimingResult benchmarkMultipleGraphs(
    const char* family,
    const std::function<CsrGraph()>& make_graph,
    const uint32_t num_graphs
) {
    RawSamples merged{};
    TimingResult metadata{};
    metadata.verify_trunc_ok = true;
    metadata.verify_full_ok = true;

    for (uint32_t graph_index = 0U; graph_index < num_graphs; ++graph_index) {
        const CsrGraph graph = make_graph();
        const uint32_t warmup = warmupIterations(graph.numVertices());
        const uint32_t measured = measuredIterations(graph.numVertices());
        const RawSamples samples = measureGraph(graph, warmup, measured);
        recordVerification(family, graph, samples.verify);
        metadata.verify_trunc_ok =
            metadata.verify_trunc_ok && samples.verify.trunc_matches;
        metadata.verify_full_ok = metadata.verify_full_ok && samples.verify.full_matches;
        appendSamples(merged, samples);

        if (graph_index == 0U) {
            metadata.num_vertices = graph.numVertices();
            metadata.num_edges = graph.numEdges();
            if (graph.numVertices() > 1U) {
                const double denom = static_cast<double>(graph.numVertices())
                    * static_cast<double>(graph.numVertices() - 1U);
                metadata.edge_density = static_cast<double>(metadata.num_edges) / denom;
            }
            metadata.largest_component = largestUndirectedComponentSize(graph);
            metadata.squaring_count_trunc =
                BooleanClosure::kleeneSquaringCountForLargestComponent(
                    metadata.largest_component
                );
            metadata.squaring_count_full = fullSquaringCount(graph.numVertices());
        }
    }

    metadata.cc = computeStats(std::move(merged.cc));
    metadata.warshall = computeStats(std::move(merged.warshall));
    metadata.kleene_trunc = computeStats(std::move(merged.kleene_trunc));
    metadata.kleene_full = computeStats(std::move(merged.kleene_full));
    return metadata;
}

void printCsvHeader() {
    std::printf(
        "family,M,edges,density,n_max,k_trunc,k_full,verify_trunc,verify_full,"
        "cc_p50_us,warshall_p50_us,kleene_trunc_p50_us,kleene_full_p50_us,"
        "ratio_trunc,ratio_full,"
        "warshall_p10_us,warshall_p90_us,kleene_trunc_p10_us,kleene_trunc_p90_us\n"
    );
}

void printCsvRow(const char* family, const TimingResult& result) {
    const auto to_us = [](const uint64_t ns) {
        return static_cast<double>(ns) / 1000.0;
    };
    const double cc_us = to_us(result.cc.p50);
    const double w_us = to_us(result.warshall.p50);
    const double k_trunc_us = to_us(result.kleene_trunc.p50);
    const double k_full_us = to_us(result.kleene_full.p50);
    const double ratio_trunc = w_us > 0.0 ? (cc_us + k_trunc_us) / w_us : 0.0;
    const double ratio_full = w_us > 0.0 ? (cc_us + k_full_us) / w_us : 0.0;

    std::printf(
        "%s,%u,%llu,%.4f,%u,%u,%u,%u,%u,"
        "%.3f,%.3f,%.3f,%.3f,"
        "%.3f,%.3f,"
        "%.3f,%.3f,%.3f,%.3f\n",
        family,
        result.num_vertices,
        static_cast<unsigned long long>(result.num_edges),
        result.edge_density,
        result.largest_component,
        result.squaring_count_trunc,
        result.squaring_count_full,
        result.verify_trunc_ok ? 1U : 0U,
        result.verify_full_ok ? 1U : 0U,
        cc_us,
        w_us,
        k_trunc_us,
        k_full_us,
        ratio_trunc,
        ratio_full,
        to_us(result.warshall.p10),
        to_us(result.warshall.p90),
        to_us(result.kleene_trunc.p10),
        to_us(result.kleene_trunc.p90)
    );
}

void runFamilyRandomEr(std::mt19937_64& rng, const uint32_t num_seeds) {
    const uint32_t sizes[] = {
        4U, 8U, 12U, 16U, 20U, 24U, 32U, 48U, 56U, 64U, 70U, 96U, 128U, 192U, 256U
    };
    const double densities[] = {0.02, 0.05, 0.10, 0.15, 0.25, 0.40, 0.60};

    for (const uint32_t size : sizes) {
        for (const double density : densities) {
            char family[64];
            std::snprintf(family, sizeof(family), "random_er_p%.2f", density);
            printCsvRow(
                family,
                benchmarkMultipleGraphs(
                    family,
                    [&]() { return makeRandomDirectedGraph(size, density, rng); },
                    num_seeds
                )
            );
        }
    }
}

void runFamilyDisconnected(std::mt19937_64& rng, const uint32_t num_seeds) {
    const uint32_t sizes[] = {16U, 32U, 64U, 96U, 128U};
    const uint32_t component_counts[] = {2U, 4U, 8U, 16U};

    for (const uint32_t size : sizes) {
        for (const uint32_t components : component_counts) {
            if (components > size / 2U) {
                continue;
            }
            char family[64];
            std::snprintf(
                family,
                sizeof(family),
                "disc_%u_comp_p0.15",
                components
            );
            printCsvRow(
                family,
                benchmarkMultipleGraphs(
                    family,
                    [&]() {
                        return makeDisconnectedRandomGraph(size, components, 0.15, rng);
                    },
                    num_seeds
                )
            );
        }
    }

    for (const uint32_t size : sizes) {
        const uint32_t many_components = std::max(2U, size / 8U);
        char family[64];
        std::snprintf(
            family,
            sizeof(family),
            "disc_%u_tiny_p0.10",
            many_components
        );
        printCsvRow(
            family,
            benchmarkMultipleGraphs(
                family,
                [&]() {
                    return makeDisconnectedRandomGraph(size, many_components, 0.10, rng);
                },
                num_seeds
            )
        );
    }
}

void runFamilyStructured(const uint32_t num_seeds) {
    const uint32_t sizes[] = {
        4U, 8U, 16U, 32U, 48U, 64U, 70U, 96U, 128U, 192U, 256U
    };

    for (const uint32_t size : sizes) {
        printCsvRow(
            "chain",
            benchmarkGraph(
                "chain",
                makeDirectedChain(size),
                warmupIterations(size),
                measuredIterations(size)
            )
        );
        printCsvRow(
            "cycle",
            benchmarkGraph(
                "cycle",
                makeDirectedCycle(size),
                warmupIterations(size),
                measuredIterations(size)
            )
        );
        printCsvRow(
            "complete",
            benchmarkGraph(
                "complete",
                makeCompleteDirected(size),
                warmupIterations(size),
                measuredIterations(size)
            )
        );
        printCsvRow(
            "star_out",
            benchmarkGraph(
                "star_out",
                makeStarOut(size),
                warmupIterations(size),
                measuredIterations(size)
            )
        );
        printCsvRow(
            "star_in",
            benchmarkGraph(
                "star_in",
                makeStarIn(size),
                warmupIterations(size),
                measuredIterations(size)
            )
        );
        (void)num_seeds;
    }
}

void runFamilyBipartite(std::mt19937_64& rng, const uint32_t num_seeds) {
    const uint32_t sizes[] = {16U, 32U, 64U, 96U, 128U};

    for (const uint32_t size : sizes) {
        char family[64];
        std::snprintf(family, sizeof(family), "bipartite_p0.20");
        printCsvRow(
            family,
            benchmarkMultipleGraphs(
                family,
                [&]() { return makeBipartiteRandom(size, 0.20, rng); },
                num_seeds
            )
        );
        std::snprintf(family, sizeof(family), "bipartite_p0.50");
        printCsvRow(
            family,
            benchmarkMultipleGraphs(
                family,
                [&]() { return makeBipartiteRandom(size, 0.50, rng); },
                num_seeds
            )
        );
    }
}

void runFamilyGrid(std::mt19937_64& rng, const uint32_t num_seeds) {
    struct GridSpec {
        uint32_t width;
        uint32_t height;
        double wall_probability;
        bool east_south_dag;
        const char* family;
    };

    const GridSpec specs[] = {
        {8U, 8U, 0.00, true, "grid8x8_es_dag_open"},
        {8U, 8U, 0.15, true, "grid8x8_es_dag_w15"},
        {8U, 8U, 0.35, true, "grid8x8_es_dag_w35"},
        {10U, 10U, 0.15, true, "grid10x10_es_dag_w15"},
        {12U, 12U, 0.15, true, "grid12x12_es_dag_w15"},
        {8U, 8U, 0.15, false, "grid8x8_rand_orient_w15"},
        {10U, 10U, 0.15, false, "grid10x10_rand_orient_w15"},
        {12U, 12U, 0.15, false, "grid12x12_rand_orient_w15"},
        {16U, 16U, 0.15, false, "grid16x16_rand_orient_w15"},
    };

    for (const GridSpec& spec : specs) {
        printCsvRow(
            spec.family,
            benchmarkMultipleGraphs(
                spec.family,
                [&]() {
                    if (spec.east_south_dag) {
                        return makeGridEastSouthDag(
                            spec.width,
                            spec.height,
                            spec.wall_probability,
                            rng
                        );
                    }
                    return makeGridRandomOrientation(
                        spec.width,
                        spec.height,
                        spec.wall_probability,
                        rng
                    );
                },
                num_seeds
            )
        );
    }
}

void printSummary(const char* title, const std::vector<std::pair<const char*, TimingResult>>& rows) {
    std::printf("\n=== %s ===\n", title);
    std::printf(
        "%-28s %4s %6s %5s %2s %2s %10s %10s %10s %7s\n",
        "family",
        "M",
        "edges",
        "nmax",
        "kt",
        "kf",
        "warshall",
        "kleene_t",
        "kleene_f",
        "K/W"
    );
    for (const auto& [family, result] : rows) {
        const double w = static_cast<double>(result.warshall.p50) / 1000.0;
        const double kt =
            (static_cast<double>(result.cc.p50 + result.kleene_trunc.p50)) / 1000.0;
        const double kf =
            (static_cast<double>(result.cc.p50 + result.kleene_full.p50)) / 1000.0;
        const double ratio = w > 0.0 ? kt / w : 0.0;
        std::printf(
            "%-28s %4u %6llu %5u %2u %2u %9.2fus %9.2fus %9.2fus %6.2fx\n",
            family,
            result.num_vertices,
            static_cast<unsigned long long>(result.num_edges),
            result.largest_component,
            result.squaring_count_trunc,
            result.squaring_count_full,
            w,
            kt,
            kf,
            ratio
        );
    }
}

void runHighlights(std::mt19937_64& rng) {
    std::vector<std::pair<const char*, TimingResult>> highlights;

    auto add = [&](const char* label, const std::function<CsrGraph()>& make_graph) {
        highlights.emplace_back(label, benchmarkMultipleGraphs(label, make_graph, 3U));
    };

    add("random M=64 p=0.15", [&]() { return makeRandomDirectedGraph(64U, 0.15, rng); });
    add("random M=128 p=0.15", [&]() { return makeRandomDirectedGraph(128U, 0.15, rng); });
    add(
        "disc 16 comp M=128",
        [&]() { return makeDisconnectedRandomGraph(128U, 16U, 0.10, rng); }
    );
    add("chain M=128", [&]() { return makeDirectedChain(128U); });
    add("complete M=32", [&]() { return makeCompleteDirected(32U); });
    add(
        "grid8x8 es dag",
        [&]() { return makeGridEastSouthDag(8U, 8U, 0.15, rng); }
    );
    add(
        "grid12x12 rand",
        [&]() { return makeGridRandomOrientation(12U, 12U, 0.15, rng); }
    );
    add("M=70 tail bits", [&]() { return makeRandomDirectedGraph(70U, 0.15, rng); });

    printSummary("Highlight cases (3 graphs, pooled p50)", highlights);
}

}  // namespace

int main() {
    std::mt19937_64 rng{0xC1A5EULL};
    constexpr uint32_t kSeeds = 3U;

    std::printf("# Kleene squaring vs Warshall closure benchmark\n");
    std::printf("# Build: Release recommended. ratio = (cc + kleene) / warshall (lower is better for Kleene).\n");
    std::printf("# k_trunc uses ceil(log2(n_max)); k_full uses ceil(log2(M)).\n");
    std::printf("# verify_* columns: 1 when Kleene closure matches Warshall on that graph.\n\n");

    printCsvHeader();
    runFamilyRandomEr(rng, kSeeds);
    runFamilyDisconnected(rng, kSeeds);
    runFamilyStructured(kSeeds);
    runFamilyBipartite(rng, kSeeds);
    runFamilyGrid(rng, kSeeds);
    runHighlights(rng);

    std::printf(
        "\n# Completed %u independent graphs per stochastic family (samples pooled).\n",
        kSeeds
    );
    std::printf(
        "# Correctness: checked %u graphs; k_trunc mismatches=%u; k_full mismatches=%u\n",
        g_verify_counters.graphs_checked,
        g_verify_counters.trunc_mismatches,
        g_verify_counters.full_mismatches
    );
    if (g_verify_counters.trunc_mismatches != 0U || g_verify_counters.full_mismatches != 0U) {
        return 1;
    }
    return 0;
}
