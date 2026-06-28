/**
 * @file port_closure_benchmark.cpp
 * @brief Flat BRICK port-graph Kleene vs Warshall closure timing and BFS query comparison.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <random>
#include <vector>

#include "hbrick/baselines/brick_closure_baseline.hpp"
#include "hbrick/baselines/brick_search_baseline.hpp"
#include "hbrick/baselines/closure_matrix_builder.hpp"
#include "hbrick/bench/bench_timer.hpp"
#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/core/grid_coord.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/connected_components.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

using hbrick::BaselineStatus;
using hbrick::BenchTimer;
using hbrick::BitMatrix;
using hbrick::BooleanClosure;
using hbrick::BrickClosureBaseline;
using hbrick::BrickIndex;
using hbrick::BrickSearchBaseline;
using hbrick::Bfs;
using hbrick::ClosureMatrixBuilder;
using hbrick::CsrGraph;
using hbrick::DirectedGridGraph;
using hbrick::DirectedGridGraphBuilder;
using hbrick::GraphSearchScratch;
using hbrick::GridEdgeConversionMode;
using hbrick::MazeLayout;
using hbrick::RandomAsymmetricParams;
using hbrick::ReachabilityAnswer;
using hbrick::TileSize;
using hbrick::bitMatricesEqual;
using hbrick::largestUndirectedComponentSize;

constexpr uint32_t kExhaustiveCellQueryVertexLimit = 1024U;
constexpr uint32_t kExhaustivePortPairLimit = 512U;
constexpr uint32_t kMinRandomCellQueries = 65536U;
constexpr uint32_t kMaxRandomCellQueries = 524288U;
constexpr uint32_t kMinRandomPortQueries = 16384U;
constexpr uint32_t kMaxRandomPortQueries = 131072U;

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

struct QueryPair {
    uint32_t source = 0U;
    uint32_t target = 0U;
};

struct VerifyCounters {
    uint32_t graphs_checked = 0U;
    uint32_t closure_kleene_warshall_mismatches = 0U;
    uint32_t closure_kleene_full_mismatches = 0U;
    uint32_t closure_property_failures = 0U;
    uint32_t baseline_matrix_mismatches = 0U;
    uint32_t port_bfs_mismatches = 0U;
    uint64_t port_pairs_checked = 0U;
    uint64_t cell_queries_checked = 0U;
    uint32_t cell_query_mismatches = 0U;
};

VerifyCounters g_verify{};

struct TimingResult {
    const char* family = "";
    uint32_t map_w = 0U;
    uint32_t map_h = 0U;
    uint32_t tile_w = 0U;
    uint32_t tile_h = 0U;
    uint32_t num_vertices = 0U;
    uint32_t num_ports = 0U;
    uint64_t port_edges = 0U;
    uint32_t n_max = 0U;
    uint32_t k_trunc = 0U;
    uint32_t k_full = 0U;
    bool verify_ok = true;
    uint32_t query_pairs_verified = 0U;
    uint32_t port_pairs_verified = 0U;
    SampleStats cc{};
    SampleStats index_build{};
    SampleStats kleene_closure{};
    SampleStats warshall_closure{};
    SampleStats brick_closure_query{};
    SampleStats brick_search_query{};
    SampleStats bfs_query{};
};

[[nodiscard]] uint32_t warmupIterations(const uint32_t num_ports) {
    if (num_ports <= 32U) {
        return 15U;
    }
    if (num_ports <= 128U) {
        return 8U;
    }
    if (num_ports <= 512U) {
        return 4U;
    }
    return 2U;
}

[[nodiscard]] uint32_t closureMeasuredIterations(const uint32_t num_ports) {
    if (num_ports <= 32U) {
        return 800U;
    }
    if (num_ports <= 64U) {
        return 400U;
    }
    if (num_ports <= 128U) {
        return 150U;
    }
    if (num_ports <= 256U) {
        return 60U;
    }
    if (num_ports <= 512U) {
        return 25U;
    }
    return 10U;
}

[[nodiscard]] uint32_t queryMeasuredIterations(const uint32_t num_vertices) {
    if (num_vertices <= 64U) {
        return 5000U;
    }
    if (num_vertices <= 256U) {
        return 3000U;
    }
    if (num_vertices <= 1024U) {
        return 2000U;
    }
    if (num_vertices <= 4096U) {
        return 1000U;
    }
    return 500U;
}

[[nodiscard]] uint32_t fullSquaringCount(const uint32_t num_vertices) noexcept {
    return BooleanClosure::kleeneSquaringCountForLargestComponent(num_vertices);
}

[[nodiscard]] std::vector<QueryPair> makeAllCellPairs(const uint32_t num_vertices) {
    std::vector<QueryPair> pairs;
    pairs.reserve(static_cast<std::size_t>(num_vertices) * static_cast<std::size_t>(num_vertices));
    for (uint32_t source = 0U; source < num_vertices; ++source) {
        for (uint32_t target = 0U; target < num_vertices; ++target) {
            pairs.push_back(QueryPair{source, target});
        }
    }
    return pairs;
}

[[nodiscard]] std::vector<QueryPair> makeRandomCellPairs(
    const uint32_t num_vertices,
    const uint32_t num_pairs,
    std::mt19937_64& rng
) {
    std::vector<QueryPair> pairs;
    pairs.reserve(num_pairs);
    std::uniform_int_distribution<uint32_t> vertex_dist(0U, num_vertices - 1U);
    for (uint32_t index = 0U; index < num_pairs; ++index) {
        pairs.push_back(QueryPair{vertex_dist(rng), vertex_dist(rng)});
    }
    return pairs;
}

[[nodiscard]] std::vector<QueryPair> makeStructuredCellPairs(const uint32_t num_vertices) {
    std::vector<QueryPair> pairs;
    if (num_vertices == 0U) {
        return pairs;
    }

    const uint32_t last = num_vertices - 1U;
    const uint32_t mid = num_vertices / 2U;
    const QueryPair seeds[] = {
        {0U, 0U},
        {0U, last},
        {last, 0U},
        {last, last},
        {0U, mid},
        {mid, last},
        {mid, mid},
    };
    for (const QueryPair& seed : seeds) {
        if (seed.source < num_vertices && seed.target < num_vertices) {
            pairs.push_back(seed);
        }
    }

    const uint32_t stride = std::max(1U, num_vertices / 32U);
    for (uint32_t source = 0U; source < num_vertices; source += stride) {
        for (uint32_t target = 0U; target < num_vertices; target += stride) {
            pairs.push_back(QueryPair{source, target});
        }
    }
    return pairs;
}

[[nodiscard]] std::vector<QueryPair> buildCellVerificationPairs(
    const uint32_t num_vertices,
    std::mt19937_64& rng
) {
    if (num_vertices <= kExhaustiveCellQueryVertexLimit) {
        return makeAllCellPairs(num_vertices);
    }

    std::vector<QueryPair> pairs = makeStructuredCellPairs(num_vertices);
    const uint32_t random_count = std::clamp(
        std::max(kMinRandomCellQueries, num_vertices * 16U),
        kMinRandomCellQueries,
        kMaxRandomCellQueries
    );
    const std::vector<QueryPair> random_pairs =
        makeRandomCellPairs(num_vertices, random_count, rng);
    pairs.insert(pairs.end(), random_pairs.begin(), random_pairs.end());
    return pairs;
}

[[nodiscard]] bool verifyClosureMatrixReflexive(
    const BitMatrix& closure,
    const char* family
) {
    for (uint32_t vertex = 0U; vertex < closure.numRows(); ++vertex) {
        if (!closure.test(vertex, vertex)) {
            std::fprintf(
                stderr,
                "CLOSURE PROPERTY FAIL (reflexive): family=%s vertex=%u\n",
                family,
                vertex
            );
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool verifyClosureMatrixTransitiveSample(
    const BitMatrix& closure,
    const char* family,
    std::mt19937_64& rng
) {
    if (closure.numRows() == 0U) {
        return true;
    }

    std::uniform_int_distribution<uint32_t> vertex_dist(0U, closure.numRows() - 1U);
    constexpr uint32_t kSamples = 4096U;
    for (uint32_t sample = 0U; sample < kSamples; ++sample) {
        const uint32_t i = vertex_dist(rng);
        const uint32_t j = vertex_dist(rng);
        const uint32_t k = vertex_dist(rng);
        if (closure.test(i, j) && closure.test(j, k) && !closure.test(i, k)) {
            std::fprintf(
                stderr,
                "CLOSURE PROPERTY FAIL (transitive): family=%s i=%u j=%u k=%u\n",
                family,
                i,
                j,
                k
            );
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool verifyPortClosureAgainstBfs(
    const char* family,
    const CsrGraph& port_graph,
    const BitMatrix& port_closure,
    std::mt19937_64& rng,
    uint32_t& port_pairs_verified
) {
    const uint32_t num_ports = port_graph.numVertices();
    if (num_ports == 0U
        || port_closure.numRows() != num_ports
        || port_closure.numCols() != num_ports) {
        return false;
    }

    GraphSearchScratch scratch(num_ports);
    const auto check_pair = [&](const uint32_t source, const uint32_t target) -> bool {
        ++g_verify.port_pairs_checked;
        const ReachabilityAnswer bfs_answer =
            Bfs::reachable(port_graph, source, target, scratch);
        const bool closure_reachable = port_closure.test(source, target);
        const bool expected = bfs_answer == ReachabilityAnswer::Reachable;
        if (closure_reachable != expected) {
            std::fprintf(
                stderr,
                "PORT BFS FAIL: family=%s p=%u q=%u bfs=%d closure=%d\n",
                family,
                source,
                target,
                static_cast<int>(bfs_answer),
                static_cast<int>(closure_reachable)
            );
            return false;
        }
        return true;
    };

    if (num_ports <= kExhaustivePortPairLimit) {
        port_pairs_verified = num_ports * num_ports;
        for (uint32_t source = 0U; source < num_ports; ++source) {
            for (uint32_t target = 0U; target < num_ports; ++target) {
                if (!check_pair(source, target)) {
                    return false;
                }
            }
        }
        return true;
    }

    const uint32_t random_count = std::clamp(
        std::max(kMinRandomPortQueries, num_ports * 32U),
        kMinRandomPortQueries,
        kMaxRandomPortQueries
    );
    port_pairs_verified = random_count;
    std::uniform_int_distribution<uint32_t> port_dist(0U, num_ports - 1U);
    for (uint32_t sample = 0U; sample < random_count; ++sample) {
        if (!check_pair(port_dist(rng), port_dist(rng))) {
            return false;
        }
    }

    const uint32_t stride = std::max(1U, num_ports / 32U);
    for (uint32_t source = 0U; source < num_ports; source += stride) {
        for (uint32_t target = 0U; target < num_ports; target += stride) {
            ++port_pairs_verified;
            if (!check_pair(source, target)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool verifyCellQueries(
    const char* family,
    const DirectedGridGraph& graph,
    const BrickClosureBaseline& closure_baseline,
    const BrickSearchBaseline& search_baseline,
    const std::vector<QueryPair>& pairs
) {
    GraphSearchScratch scratch(graph.numVertices());
    const CsrGraph& csr = graph.csrGraph();

    for (const QueryPair& pair : pairs) {
        ++g_verify.cell_queries_checked;
        const ReachabilityAnswer expected =
            Bfs::reachable(csr, pair.source, pair.target, scratch);
        const ReachabilityAnswer closure_answer =
            closure_baseline.query(pair.source, pair.target);
        const ReachabilityAnswer search_answer =
            search_baseline.query(pair.source, pair.target);

        if (closure_answer != expected || search_answer != expected) {
            std::fprintf(
                stderr,
                "CELL QUERY FAIL: family=%s s=%u t=%u bfs=%d kleene=%d search=%d\n",
                family,
                pair.source,
                pair.target,
                static_cast<int>(expected),
                static_cast<int>(closure_answer),
                static_cast<int>(search_answer)
            );
            return false;
        }
    }
    return true;
}

[[nodiscard]] TimingResult benchmarkCase(
    const char* family,
    const MazeLayout& layout,
    const DirectedGridGraph& graph,
    const TileSize tile_size,
    std::mt19937_64& rng
) {
    constexpr uint64_t kMaxMemory = std::numeric_limits<uint64_t>::max();

    TimingResult result{};
    result.family = family;
    result.map_w = graph.width();
    result.map_h = graph.height();
    result.tile_w = tile_size.width;
    result.tile_h = tile_size.height;
    result.num_vertices = graph.numVertices();

    BenchTimer timer{};

    timer.start();
    const BrickIndex index = BrickIndex::build(graph, layout, tile_size, kMaxMemory);
    timer.stop();
    result.index_build.p50 = timer.elapsedNanoseconds();

    if (index.status() != BaselineStatus::Completed) {
        result.verify_ok = false;
        return result;
    }

    ++g_verify.graphs_checked;

    const CsrGraph& port_graph = index.portGraph();
    result.num_ports = port_graph.numVertices();
    result.port_edges = port_graph.numEdges();
    result.n_max = largestUndirectedComponentSize(port_graph);
    result.k_trunc =
        BooleanClosure::kleeneSquaringCountForLargestComponent(result.n_max);
    result.k_full = fullSquaringCount(result.num_ports);

    const BitMatrix port_base = ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(
        port_graph,
        kMaxMemory
    );

    BitMatrix kleene_trunc = port_base;
    BitMatrix kleene_scratch{};
    ClosureMatrixBuilder::transitiveClosureKleeneTruncatedInPlace(
        kleene_trunc,
        port_graph,
        &kleene_scratch
    );

    BitMatrix warshall = port_base;
    ClosureMatrixBuilder::transitiveClosureWarshallOracleInPlace(warshall);

    if (!bitMatricesEqual(kleene_trunc, warshall)) {
        result.verify_ok = false;
        ++g_verify.closure_kleene_warshall_mismatches;
        std::fprintf(
            stderr,
            "KLEENE/WARSHALL FAIL: family=%s P=%u n_max=%u k=%u\n",
            family,
            result.num_ports,
            result.n_max,
            result.k_trunc
        );
    }

    if (result.k_trunc != result.k_full) {
        BitMatrix kleene_full = port_base;
        BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            kleene_full,
            result.k_full,
            &kleene_scratch
        );
        if (!bitMatricesEqual(kleene_full, warshall)) {
            result.verify_ok = false;
            ++g_verify.closure_kleene_full_mismatches;
            std::fprintf(
                stderr,
                "KLEENE FULL FAIL: family=%s P=%u k_full=%u\n",
                family,
                result.num_ports,
                result.k_full
            );
        }
    }

    if (!verifyClosureMatrixReflexive(kleene_trunc, family)
        || !verifyClosureMatrixTransitiveSample(kleene_trunc, family, rng)) {
        result.verify_ok = false;
        ++g_verify.closure_property_failures;
    }

    if (!verifyPortClosureAgainstBfs(
            family,
            port_graph,
            kleene_trunc,
            rng,
            result.port_pairs_verified)) {
        result.verify_ok = false;
        ++g_verify.port_bfs_mismatches;
    }

    const uint32_t warm = warmupIterations(result.num_ports);
    const uint32_t closure_iters = closureMeasuredIterations(result.num_ports);

    std::vector<uint64_t> cc_samples;
    cc_samples.reserve(closure_iters);
    for (uint32_t iteration = 0U; iteration < closure_iters; ++iteration) {
        timer.start();
        (void)largestUndirectedComponentSize(port_graph);
        timer.stop();
        cc_samples.push_back(timer.elapsedNanoseconds());
    }
    result.cc = computeStats(std::move(cc_samples));

    for (uint32_t iteration = 0U; iteration < warm; ++iteration) {
        BitMatrix relation = port_base;
        ClosureMatrixBuilder::transitiveClosureKleeneTruncatedInPlace(
            relation,
            port_graph,
            &kleene_scratch
        );
    }

    std::vector<uint64_t> kleene_samples;
    kleene_samples.reserve(closure_iters);
    for (uint32_t iteration = 0U; iteration < closure_iters; ++iteration) {
        BitMatrix relation = port_base;
        timer.start();
        ClosureMatrixBuilder::transitiveClosureKleeneTruncatedInPlace(
            relation,
            port_graph,
            &kleene_scratch
        );
        timer.stop();
        kleene_samples.push_back(timer.elapsedNanoseconds());
    }
    result.kleene_closure = computeStats(std::move(kleene_samples));

    for (uint32_t iteration = 0U; iteration < warm; ++iteration) {
        BitMatrix relation = port_base;
        ClosureMatrixBuilder::transitiveClosureWarshallOracleInPlace(relation);
    }

    std::vector<uint64_t> warshall_samples;
    warshall_samples.reserve(closure_iters);
    for (uint32_t iteration = 0U; iteration < closure_iters; ++iteration) {
        BitMatrix relation = port_base;
        timer.start();
        ClosureMatrixBuilder::transitiveClosureWarshallOracleInPlace(relation);
        timer.stop();
        warshall_samples.push_back(timer.elapsedNanoseconds());
    }
    result.warshall_closure = computeStats(std::move(warshall_samples));

    BrickClosureBaseline closure_baseline;
    closure_baseline.preprocess(graph, layout, tile_size, kMaxMemory);
    BrickSearchBaseline search_baseline;
    search_baseline.preprocess(graph, layout, tile_size, kMaxMemory);

    if (closure_baseline.status() != BaselineStatus::Completed
        || search_baseline.status() != BaselineStatus::Completed) {
        result.verify_ok = false;
        return result;
    }

    if (!bitMatricesEqual(closure_baseline.portClosure(), kleene_trunc)) {
        result.verify_ok = false;
        ++g_verify.baseline_matrix_mismatches;
        std::fprintf(
            stderr,
            "BASELINE MATRIX FAIL: family=%s P=%u\n",
            family,
            result.num_ports
        );
    }

    const std::vector<QueryPair> verification_pairs =
        buildCellVerificationPairs(result.num_vertices, rng);
    result.query_pairs_verified = static_cast<uint32_t>(verification_pairs.size());

    if (!verifyCellQueries(
            family,
            graph,
            closure_baseline,
            search_baseline,
            verification_pairs)) {
        result.verify_ok = false;
        ++g_verify.cell_query_mismatches;
    }

    GraphSearchScratch bfs_scratch(graph.numVertices());
    const CsrGraph& csr = graph.csrGraph();
    const uint32_t query_iters = queryMeasuredIterations(result.num_vertices);
    const uint32_t query_warm = std::min(100U, query_iters / 4U);

    for (uint32_t iteration = 0U; iteration < query_warm; ++iteration) {
        const QueryPair& pair =
            verification_pairs[iteration % verification_pairs.size()];
        (void)closure_baseline.query(pair.source, pair.target);
        (void)search_baseline.query(pair.source, pair.target);
        (void)Bfs::reachable(csr, pair.source, pair.target, bfs_scratch);
    }

    std::vector<uint64_t> closure_query_samples;
    closure_query_samples.reserve(query_iters);
    for (uint32_t iteration = 0U; iteration < query_iters; ++iteration) {
        const QueryPair& pair =
            verification_pairs[iteration % verification_pairs.size()];
        timer.start();
        (void)closure_baseline.query(pair.source, pair.target);
        timer.stop();
        closure_query_samples.push_back(timer.elapsedNanoseconds());
    }
    result.brick_closure_query = computeStats(std::move(closure_query_samples));

    std::vector<uint64_t> search_query_samples;
    search_query_samples.reserve(query_iters);
    for (uint32_t iteration = 0U; iteration < query_iters; ++iteration) {
        const QueryPair& pair =
            verification_pairs[iteration % verification_pairs.size()];
        timer.start();
        (void)search_baseline.query(pair.source, pair.target);
        timer.stop();
        search_query_samples.push_back(timer.elapsedNanoseconds());
    }
    result.brick_search_query = computeStats(std::move(search_query_samples));

    std::vector<uint64_t> bfs_samples;
    bfs_samples.reserve(query_iters);
    for (uint32_t iteration = 0U; iteration < query_iters; ++iteration) {
        const QueryPair& pair =
            verification_pairs[iteration % verification_pairs.size()];
        timer.start();
        (void)Bfs::reachable(csr, pair.source, pair.target, bfs_scratch);
        timer.stop();
        bfs_samples.push_back(timer.elapsedNanoseconds());
    }
    result.bfs_query = computeStats(std::move(bfs_samples));

    return result;
}

void printCsvHeader() {
    std::printf(
        "family,map_w,map_h,tile_w,tile_h,V,P,port_edges,n_max,k_trunc,k_full,"
        "verify_ok,cell_queries_verified,port_pairs_verified,"
        "index_build_us,cc_p50_us,kleene_p50_us,warshall_p50_us,"
        "kleene_over_warshall,brick_closure_q_us,brick_search_q_us,bfs_q_us,"
        "closure_q_over_bfs,search_q_over_bfs\n"
    );
}

void printCsvRow(const TimingResult& result) {
    const auto to_us = [](const uint64_t ns) -> double {
        return static_cast<double>(ns) / 1000.0;
    };
    const double kleene_us = to_us(result.kleene_closure.p50);
    const double w_us = to_us(result.warshall_closure.p50);
    const double closure_q_us = to_us(result.brick_closure_query.p50);
    const double search_q_us = to_us(result.brick_search_query.p50);
    const double bfs_q_us = to_us(result.bfs_query.p50);

    std::printf(
        "%s,%u,%u,%u,%u,%u,%u,%llu,%u,%u,%u,%u,%u,%u,"
        "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
        result.family,
        result.map_w,
        result.map_h,
        result.tile_w,
        result.tile_h,
        result.num_vertices,
        result.num_ports,
        static_cast<unsigned long long>(result.port_edges),
        result.n_max,
        result.k_trunc,
        result.k_full,
        result.verify_ok ? 1U : 0U,
        result.query_pairs_verified,
        result.port_pairs_verified,
        to_us(result.index_build.p50),
        to_us(result.cc.p50),
        kleene_us,
        w_us,
        w_us > 0.0 ? kleene_us / w_us : 0.0,
        closure_q_us,
        search_q_us,
        bfs_q_us,
        bfs_q_us > 0.0 ? closure_q_us / bfs_q_us : 0.0,
        bfs_q_us > 0.0 ? search_q_us / bfs_q_us : 0.0
    );
}

void printSummary(
    const char* title,
    const std::vector<TimingResult>& rows
) {
    std::printf("\n=== %s ===\n", title);
    std::printf(
        "%-28s %4s %4s %4s %5s %4s %2s %8s %8s %8s %7s %7s %7s %8s\n",
        "family",
        "map",
        "tile",
        "P",
        "nmax",
        "kt",
        "V",
        "kleene",
        "warsh",
        "K/W",
        "brClos",
        "brSrch",
        "bfs",
        "queries"
    );
    for (const TimingResult& result : rows) {
        const auto to_us = [](const uint64_t ns) -> double {
            return static_cast<double>(ns) / 1000.0;
        };
        const double kleene = to_us(result.kleene_closure.p50);
        const double w = to_us(result.warshall_closure.p50);
        const double ratio = w > 0.0 ? kleene / w : 0.0;
        std::printf(
            "%-28s %2ux%2u %2ux%2u %4u %5u %2u %4u %8.2fus %8.2fus %6.2fx %7.2fus %7.2fus %7.2fus %8u\n",
            result.family,
            result.map_w,
            result.map_h,
            result.tile_w,
            result.tile_h,
            result.num_ports,
            result.n_max,
            result.k_trunc,
            result.num_vertices,
            kleene,
            w,
            ratio,
            to_us(result.brick_closure_query.p50),
            to_us(result.brick_search_query.p50),
            to_us(result.bfs_query.p50),
            result.query_pairs_verified
        );
    }
}

[[nodiscard]] MazeLayout makeOpenLayout(const uint32_t width, const uint32_t height) {
    return MazeLayout{width, height, true};
}

[[nodiscard]] DirectedGridGraph makeBidirectionalGraph(const MazeLayout& layout) {
    return DirectedGridGraphBuilder::build(
        layout,
        GridEdgeConversionMode::BidirectionalAll
    );
}

[[nodiscard]] DirectedGridGraph makeRandomGraph(
    const MazeLayout& layout,
    const uint64_t seed
) {
    return DirectedGridGraphBuilder::build(
        layout,
        GridEdgeConversionMode::RandomAsymmetric,
        RandomAsymmetricParams{seed, 0.55, 0.45, 0.0, 0.0}
    );
}

void runGridFamily(
    std::mt19937_64& rng,
    const char* family_prefix,
    const uint32_t map_size,
    const TileSize tile_size
) {
    MazeLayout layout = makeOpenLayout(map_size, map_size);
    layout.setPassable(hbrick::GridCoord{map_size / 4U, map_size / 4U}, false);
    const DirectedGridGraph graph = makeBidirectionalGraph(layout);

    char family[64];
    std::snprintf(family, sizeof(family), "%s_%u", family_prefix, map_size);
    printCsvRow(benchmarkCase(family, layout, graph, tile_size, rng));
}

void runRandomFamily(
    std::mt19937_64& rng,
    const uint32_t map_size,
    const TileSize tile_size,
    const uint64_t seed
) {
    MazeLayout layout(map_size, map_size);
    layout.setPassable(hbrick::GridCoord{map_size / 3U, map_size / 3U}, false);
    layout.setPassable(hbrick::GridCoord{map_size / 2U, map_size / 2U}, false);
    const DirectedGridGraph graph = makeRandomGraph(layout, seed);

    char family[64];
    std::snprintf(
        family,
        sizeof(family),
        "rand_asym_%u_s%lu",
        map_size,
        static_cast<unsigned long>(seed)
    );
    printCsvRow(benchmarkCase(family, layout, graph, tile_size, rng));
}

void runDisconnectedFamily(
    std::mt19937_64& rng,
    const uint32_t map_w,
    const uint32_t map_h,
    const TileSize tile_size,
    const char* family
) {
    MazeLayout layout(map_w, map_h);
    for (uint32_t y = 0U; y < map_h; ++y) {
        for (uint32_t x = 0U; x < map_w / 2U; ++x) {
            layout.setPassable(x, y, false);
        }
    }
    const DirectedGridGraph graph = makeBidirectionalGraph(layout);
    printCsvRow(benchmarkCase(family, layout, graph, tile_size, rng));
}

void runHighlights(std::mt19937_64& rng) {
    std::vector<TimingResult> highlights;

    auto add = [&](const char* label, const MazeLayout& layout,
                   const DirectedGridGraph& graph, const TileSize tile_size) {
        highlights.push_back(benchmarkCase(label, layout, graph, tile_size, rng));
    };

    {
        MazeLayout layout = makeOpenLayout(32U, 32U);
        add("grid32_open_t4", layout, makeBidirectionalGraph(layout), TileSize{4U, 4U});
    }
    {
        MazeLayout layout = makeOpenLayout(64U, 64U);
        layout.setPassable(hbrick::GridCoord{16U, 16U}, false);
        add("grid64_open_t8", layout, makeBidirectionalGraph(layout), TileSize{8U, 8U});
    }
    {
        MazeLayout layout(48U, 48U);
        layout.setPassable(hbrick::GridCoord{12U, 12U}, false);
        add(
            "rand48_t4",
            layout,
            makeRandomGraph(layout, 42U),
            TileSize{4U, 4U}
        );
    }
    {
        MazeLayout layout(32U, 16U);
        for (uint32_t y = 0U; y < 16U; ++y) {
            for (uint32_t x = 0U; x < 16U; ++x) {
                layout.setPassable(x, y, false);
            }
        }
        add("disc32x16_t4", layout, makeBidirectionalGraph(layout), TileSize{4U, 4U});
    }

    printSummary("Highlight cases", highlights);
}

}  // namespace

int main() {
    std::mt19937_64 rng{0xB01C011ULL};

    std::printf("# Flat BRICK port-graph closure benchmark (extended verification)\n");
    std::printf("# Closure checks: Kleene trunc/full vs Warshall, reflexive/transitive, port BFS.\n");
    std::printf("# Cell queries: all-pairs when V<=%u; else structured+random (up to %u).\n",
                  kExhaustiveCellQueryVertexLimit,
                  kMaxRandomCellQueries);
    std::printf("# Port pairs: all-pairs when P<=%u; else structured+random (up to %u).\n\n",
                  kExhaustivePortPairLimit,
                  kMaxRandomPortQueries);

    printCsvHeader();

    const TileSize tile4{4U, 4U};
    const TileSize tile8{8U, 8U};

    for (const uint32_t size : {12U, 16U, 24U, 32U, 48U, 64U}) {
        runGridFamily(rng, "grid_open", size, tile4);
    }
    for (const uint32_t size : {32U, 48U, 64U, 96U}) {
        runGridFamily(rng, "grid_open_t8", size, tile8);
    }

    for (const uint64_t seed : {3U, 7U, 11U, 19U, 42U, 99U}) {
        runRandomFamily(rng, 24U, tile4, seed);
        runRandomFamily(rng, 32U, tile4, seed);
        runRandomFamily(rng, 48U, tile4, seed);
        runRandomFamily(rng, 64U, tile4, seed);
    }

    runDisconnectedFamily(rng, 32U, 32U, tile4, "disc_half_blocked_32");
    runDisconnectedFamily(rng, 64U, 32U, tile8, "disc_half_blocked_64");
    runDisconnectedFamily(rng, 48U, 24U, tile4, "disc_half_blocked_48x24");

    runHighlights(rng);

    std::printf(
        "\n# Correctness summary:\n"
        "#   graphs_checked=%u\n"
        "#   kleene_vs_warshall_mismatches=%u\n"
        "#   kleene_full_mismatches=%u\n"
        "#   closure_property_failures=%u\n"
        "#   baseline_matrix_mismatches=%u\n"
        "#   port_bfs_mismatches=%u (port_pairs_checked=%llu)\n"
        "#   cell_query_mismatches=%u (cell_queries_checked=%llu)\n",
        g_verify.graphs_checked,
        g_verify.closure_kleene_warshall_mismatches,
        g_verify.closure_kleene_full_mismatches,
        g_verify.closure_property_failures,
        g_verify.baseline_matrix_mismatches,
        g_verify.port_bfs_mismatches,
        static_cast<unsigned long long>(g_verify.port_pairs_checked),
        g_verify.cell_query_mismatches,
        static_cast<unsigned long long>(g_verify.cell_queries_checked)
    );

    const bool failed = g_verify.closure_kleene_warshall_mismatches != 0U
        || g_verify.closure_kleene_full_mismatches != 0U
        || g_verify.closure_property_failures != 0U
        || g_verify.baseline_matrix_mismatches != 0U
        || g_verify.port_bfs_mismatches != 0U
        || g_verify.cell_query_mismatches != 0U;
    return failed ? 1 : 0;
}
