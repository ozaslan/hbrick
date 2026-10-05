/**
 * @file param_sweep_benchmark.cpp
 * @brief Comprehensive parameter sweep comparing Normal H-BRICK, H-BRICK with Micro-BFS, and O'Reach.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/baselines/hbrick_fused_lift_baseline.hpp"
#include "hbrick/baselines/hbrick_fused_lift_cache_baseline.hpp"
#include "hbrick/baselines/hbrick_micro_bfs_baseline.hpp"
#include "hbrick/baselines/hbrick_skip_lift_baseline.hpp"
#include "hbrick/baselines/hbrick_scc_label_baseline.hpp"
#include "hbrick/baselines/oreach_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/graph/scc_decomposition.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"
#include "../../tests/support/maze_generator.hpp"

namespace {

using namespace hbrick;
using Clock = std::chrono::steady_clock;

struct BenchResult {
    std::string method;
    std::string config_desc;
    uint32_t map_d = 0;
    uint32_t tile_b = 0;
    uint32_t group_g = 0;
    double pre_ms = 0.0;
    double idx_kib = 0.0;
    double qry_ns = 0.0;
    double qps = 0.0;
    double speedup_vs_bfs = 1.0;
    std::string status = "OK";
};

template <typename F>
double measureMs(F&& fn) {
    const auto t0 = Clock::now();
    fn();
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

template <typename F>
double measureNsPerQuery(F&& fn, uint64_t total_queries, uint32_t iterations = 1) {
    const auto t0 = Clock::now();
    for (uint32_t it = 0; it < iterations; ++it) {
        fn();
    }
    const auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
    return static_cast<double>(total_ns) / static_cast<double>(total_queries * iterations);
}

std::vector<uint32_t> parseList(const std::string& csv, const std::vector<uint32_t>& fallback) {
    if (csv.empty()) return fallback;
    std::vector<uint32_t> list;
    std::istringstream ss(csv);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (!token.empty()) {
            list.push_back(static_cast<uint32_t>(std::stoul(token)));
        }
    }
    return list.empty() ? fallback : list;
}

}  // namespace

int main(int argc, char** argv) {
    std::string sizes_arg = "256";
    std::string tiles_arg = "16,32";
    std::string groups_arg = "2,4";
    std::string mode_arg = "dense_obstacle";
    std::string workload_arg = "balanced";
    uint32_t num_queries = 2048U;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--sizes") == 0 && i + 1 < argc) {
            sizes_arg = argv[++i];
        } else if (std::strcmp(argv[i], "--tiles") == 0 && i + 1 < argc) {
            tiles_arg = argv[++i];
        } else if (std::strcmp(argv[i], "--groups") == 0 && i + 1 < argc) {
            groups_arg = argv[++i];
        } else if (std::strcmp(argv[i], "--queries") == 0 && i + 1 < argc) {
            num_queries = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode_arg = argv[++i];
        } else if (std::strcmp(argv[i], "--workload") == 0 && i + 1 < argc) {
            workload_arg = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "  --sizes S1,S2,...    Grid dimensions (default: 256)\n"
                      << "  --tiles T1,T2,...    Tile sizes b (default: 16,32)\n"
                      << "  --groups G1,G2,...   Group sizes g (default: 2,4)\n"
                      << "  --queries N          Number of timed queries (default: 2048)\n"
                      << "  --mode M             Topology mode: dense_obstacle | rooms | flow_obstacles | drop_edges | maze | maze_drop | ow05 | legacy_sparse\n"
                      << "  --workload W         Query distribution: balanced | cross_scc | positive | random (default: balanced)\n";
            return 0;
        }
    }

    const auto map_sizes = parseList(sizes_arg, {256U});
    const auto tile_sizes = parseList(tiles_arg, {16U, 32U});
    const auto group_sizes = parseList(groups_arg, {2U, 4U});

    std::cout << "========================================================================================\n"
              << "  BENCHMARK: H-BRICK Variants vs O'Reach vs BFS [Mode: " << mode_arg << ", Workload: " << workload_arg << "]\n"
              << "========================================================================================\n"
              << "Map sizes: ";
    for (size_t i = 0; i < map_sizes.size(); ++i) std::cout << (i ? ", " : "") << map_sizes[i];
    std::cout << "\nTile sizes: ";
    for (size_t i = 0; i < tile_sizes.size(); ++i) std::cout << (i ? ", " : "") << tile_sizes[i];
    std::cout << "\nGroup sizes: ";
    for (size_t i = 0; i < group_sizes.size(); ++i) std::cout << (i ? ", " : "") << group_sizes[i];
    std::cout << "\nQueries per test: " << num_queries << "\n\n";

    for (uint32_t d : map_sizes) {
        std::cerr << "[Info] Generating " << d << "x" << d << " graph in mode '" << mode_arg << "'...\n";
        MazeLayout layout{d, d, true};
        DirectedGridGraph grid = [&]() {
            if (mode_arg == "maze") {
                const uint32_t lw = (d > 3 ? (d - 1) / 2 : 1);
                const uint32_t lh = (d > 3 ? (d - 1) / 2 : 1);
                test_support::MazeParams mp{lw, lh, 100ULL + d};
                layout = test_support::generateMazeWithExtraPassages(mp, 200ULL + d, d * 4);
                return DirectedGridGraphBuilder::build(
                    layout,
                    GridEdgeConversionMode::RandomAsymmetric,
                    RandomAsymmetricParams{300ULL + d, 0.80L, 0.20L, 45.0, 0.01L, 0.0L}
                );
            } else if (mode_arg == "maze_drop") {
                const uint32_t lw = (d > 3 ? (d - 1) / 2 : 1);
                const uint32_t lh = (d > 3 ? (d - 1) / 2 : 1);
                test_support::MazeParams mp{lw, lh, 100ULL + d};
                layout = test_support::generateMazeWithExtraPassages(mp, 200ULL + d, d * 4);
                return DirectedGridGraphBuilder::build(
                    layout,
                    GridEdgeConversionMode::RandomAsymmetric,
                    RandomAsymmetricParams{300ULL + d, 0.50L, 0.30L, 45.0, 0.01L, 0.20L}
                );
            } else if (mode_arg.rfind("dense_obs", 0) == 0 || mode_arg == "dense_obstacle") {
                uint32_t obs_pct = 25;
                if (mode_arg.size() > 9 && mode_arg.substr(0, 9) == "dense_obs") {
                    try {
                        obs_pct = static_cast<uint32_t>(std::stoul(mode_arg.substr(9)));
                    } catch (...) {}
                }
                std::mt19937_64 obs_rng{42ULL + d};
                for (uint32_t y = 0; y < d; ++y) {
                    for (uint32_t x = 0; x < d; ++x) {
                        if ((obs_rng() % 100) < obs_pct) {
                            layout.setPassable(x, y, false);
                        }
                    }
                }
                return DirectedGridGraphBuilder::build(
                    layout,
                    GridEdgeConversionMode::RandomAsymmetric,
                    RandomAsymmetricParams{100ULL + d, 0.50L, 0.35L, 45.0, 0.01L, 0.15L}
                );
            } else if (mode_arg == "rooms") {
                // 16x16 rooms with doorways
                const uint32_t room_size = 16;
                for (uint32_t y = 0; y < d; ++y) {
                    for (uint32_t x = 0; x < d; ++x) {
                        const bool on_v_wall = (x % room_size == 0);
                        const bool on_h_wall = (y % room_size == 0);
                        if (on_v_wall || on_h_wall) {
                            const bool is_v_door = on_v_wall && ((y % room_size == room_size / 2) || (y % room_size == room_size / 2 + 1));
                            const bool is_h_door = on_h_wall && ((x % room_size == room_size / 2) || (x % room_size == room_size / 2 + 1));
                            if (!is_v_door && !is_h_door) {
                                layout.setPassable(x, y, false);
                            }
                        }
                    }
                }
                return DirectedGridGraphBuilder::build(
                    layout,
                    GridEdgeConversionMode::RandomAsymmetric,
                    RandomAsymmetricParams{100ULL + d, 0.60L, 0.30L, 45.0, 0.01L, 0.10L}
                );
            } else if (mode_arg.rfind("flow_obs", 0) == 0 || mode_arg == "flow_obstacles") {
                uint32_t obs_pct = 20;
                if (mode_arg.size() > 8 && mode_arg.substr(0, 8) == "flow_obs") {
                    try {
                        obs_pct = static_cast<uint32_t>(std::stoul(mode_arg.substr(8)));
                    } catch (...) {}
                }
                std::mt19937_64 obs_rng{42ULL + d};
                for (uint32_t y = 0; y < d; ++y) {
                    for (uint32_t x = 0; x < d; ++x) {
                        if ((obs_rng() % 100) < obs_pct) {
                            layout.setPassable(x, y, false);
                        }
                    }
                }
                return DirectedGridGraphBuilder::build(
                    layout,
                    GridEdgeConversionMode::GradientFlow,
                    RandomAsymmetricParams{100ULL + d, 0.30L, 0.0L, 45.0, 0.08L, 0.0L}
                );
            } else if (mode_arg.rfind("drop", 0) == 0) {
                uint32_t drop_pct = 25;
                if (mode_arg.size() > 4) {
                    try {
                        drop_pct = static_cast<uint32_t>(std::stoul(mode_arg.substr(4)));
                    } catch (...) {}
                }
                long double p_drop = static_cast<long double>(drop_pct) / 100.0L;
                long double p_one = 0.30L;
                long double p_bi = (1.0L > p_drop + p_one ? 1.0L - p_drop - p_one : 0.20L);
                return DirectedGridGraphBuilder::build(
                    layout,
                    GridEdgeConversionMode::RandomAsymmetric,
                    RandomAsymmetricParams{100ULL + d, p_bi, p_one, 45.0, 0.01L, p_drop}
                );
            } else if (mode_arg == "legacy_sparse") {
                return DirectedGridGraphBuilder::build(
                    layout,
                    GridEdgeConversionMode::RandomAsymmetric,
                    RandomAsymmetricParams{100ULL + d, 0.35L, 0.05L, 45.0, 0.01L, -1.0L}
                );
            } else { // "ow05" fair 5% one-way / 95% bidirectional, 0% edge drop
                return DirectedGridGraphBuilder::build(
                    layout,
                    GridEdgeConversionMode::RandomAsymmetric,
                    RandomAsymmetricParams{100ULL + d, 0.95L, 0.05L, 45.0, 0.01L, 0.0L}
                );
            }
        }();

        const CsrGraph& csr = grid.csrGraph();
        const uint32_t V = csr.numVertices();
        const uint64_t E = csr.numEdges();

        // Collect passable vertices
        std::vector<uint32_t> passable_vertices;
        passable_vertices.reserve(V);
        for (uint32_t v = 0; v < V; ++v) {
            if (layout.isPassable(VertexId{v})) {
                passable_vertices.push_back(v);
            }
        }

        GraphSearchScratch bfsScratch{V};
        auto scc_decomp = SccDecomposition::compute(csr, bfsScratch);
        const uint32_t num_sccs = scc_decomp.numComponents();

        // Generate query workloads
        std::vector<std::pair<uint32_t, uint32_t>> random_pairs;
        std::vector<std::pair<uint32_t, uint32_t>> positive_pairs;
        std::vector<std::pair<uint32_t, uint32_t>> cross_scc_positive_pairs;
        std::vector<std::pair<uint32_t, uint32_t>> negative_pairs;

        std::mt19937_64 q_rng{54321ULL + d};
        std::uniform_int_distribution<size_t> p_dist(0, passable_vertices.size() - 1);

        // 1. Uniform random passable pairs
        random_pairs.reserve(num_queries);
        for (uint32_t i = 0; i < num_queries; ++i) {
            uint32_t u = passable_vertices[p_dist(q_rng)];
            uint32_t v = passable_vertices[p_dist(q_rng)];
            random_pairs.emplace_back(u, v);
        }

        // 2. Discover positive and negative pairs via BFS from random seeds
        std::vector<uint32_t> bfs_q;
        bfs_q.reserve(V);
        std::vector<uint8_t> visited(V, 0);

        uint32_t attempts = 0;
        const uint32_t max_attempts = num_queries * 4;
        while ((cross_scc_positive_pairs.size() < num_queries || negative_pairs.size() < num_queries)
               && attempts++ < max_attempts) {
            uint32_t u = passable_vertices[p_dist(q_rng)];
            bfs_q.clear();
            bfs_q.push_back(u);
            visited[u] = 1;

            size_t head = 0;
            while (head < bfs_q.size()) {
                uint32_t curr = bfs_q[head++];
                for (uint32_t nxt : csr.outNeighbors(curr)) {
                    if (!visited[nxt]) {
                        visited[nxt] = 1;
                        bfs_q.push_back(nxt);
                    }
                }
            }

            // Distinguish cross-SCC and same-SCC reachable targets
            std::vector<uint32_t> cross_targets;
            std::vector<uint32_t> same_targets;
            const uint32_t u_comp = scc_decomp.componentOf(u);
            for (size_t k = 1; k < bfs_q.size(); ++k) {
                uint32_t v = bfs_q[k];
                if (scc_decomp.componentOf(v) != u_comp) {
                    cross_targets.push_back(v);
                } else {
                    same_targets.push_back(v);
                }
            }

            for (int k = 0; k < 8 && cross_scc_positive_pairs.size() < num_queries && !cross_targets.empty(); ++k) {
                uint32_t v = cross_targets[q_rng() % cross_targets.size()];
                cross_scc_positive_pairs.emplace_back(u, v);
                positive_pairs.emplace_back(u, v);
            }
            for (int k = 0; k < 8 && positive_pairs.size() < num_queries && !same_targets.empty(); ++k) {
                uint32_t v = same_targets[q_rng() % same_targets.size()];
                positive_pairs.emplace_back(u, v);
            }

            for (int k = 0; k < 8 && negative_pairs.size() < num_queries; ++k) {
                uint32_t cand = passable_vertices[p_dist(q_rng)];
                if (!visited[cand]) {
                    negative_pairs.emplace_back(u, cand);
                }
            }

            for (uint32_t node : bfs_q) {
                visited[node] = 0;
            }
        }

        // Build target pairs based on workload argument
        std::vector<std::pair<uint32_t, uint32_t>> pairs;
        pairs.reserve(num_queries);

        if (workload_arg == "cross_scc" && !cross_scc_positive_pairs.empty()) {
            for (size_t i = 0; i < num_queries; ++i) {
                pairs.push_back(cross_scc_positive_pairs[i % cross_scc_positive_pairs.size()]);
            }
        } else if (workload_arg == "positive" && !positive_pairs.empty()) {
            for (size_t i = 0; i < num_queries; ++i) {
                pairs.push_back(positive_pairs[i % positive_pairs.size()]);
            }
        } else if (workload_arg == "balanced" && !positive_pairs.empty() && !negative_pairs.empty()) {
            const auto& pos_pool = cross_scc_positive_pairs.empty() ? positive_pairs : cross_scc_positive_pairs;
            for (size_t i = 0; i < num_queries / 2; ++i) {
                pairs.push_back(pos_pool[i % pos_pool.size()]);
            }
            for (size_t i = 0; i < (num_queries - num_queries / 2); ++i) {
                pairs.push_back(negative_pairs[i % negative_pairs.size()]);
            }
        } else { // "random"
            pairs = random_pairs;
        }

        // Measure reachability connectivity %
        uint32_t reachable_count = 0;
        for (const auto& p : pairs) {
            if (Bfs::reachable(csr, p.first, p.second, bfsScratch) == ReachabilityAnswer::Reachable) {
                ++reachable_count;
            }
        }
        const double conn_pct = (100.0 * reachable_count) / static_cast<double>(pairs.size());

        std::cout << "\n### Map: " << d << "x" << d << " (Vertices: " << V
                  << ", Passable: " << passable_vertices.size()
                  << ", Edges: " << E << ", SCCs: " << num_sccs
                  << ", Workload: '" << workload_arg << "', Reachable in workload: "
                  << std::fixed << std::setprecision(1) << conn_pct << "%)\n\n";

        std::vector<BenchResult> results;
        volatile ReachabilityAnswer sink{ReachabilityAnswer::Unreachable};

        // Print table header for map d
        std::cout << "| Method | Configuration | Preprocess (ms) | Index Memory | Query Latency | Throughput (QPS) | Speedup vs BFS |\n"
                  << "|:---|:---|---:|---:|---:|---:|---:|\n"
                  << std::flush;

        auto printRow = [](const BenchResult& r) {
            std::cout << "| " << r.method << " | " << r.config_desc << " | ";
            if (r.status == "OK") {
                std::cout << std::fixed << std::setprecision(2) << r.pre_ms << " ms | ";
                if (r.idx_kib >= 1024.0) {
                    std::cout << std::fixed << std::setprecision(2) << (r.idx_kib / 1024.0) << " MiB | ";
                } else {
                    std::cout << std::fixed << std::setprecision(1) << r.idx_kib << " KiB | ";
                }
                std::cout << std::fixed << std::setprecision(1) << r.qry_ns << " ns | "
                          << std::fixed << std::setprecision(0) << r.qps << " | "
                          << std::fixed << std::setprecision(2) << r.speedup_vs_bfs << "x |\n";
            } else {
                std::cout << "- | - | - | - | " << r.status << " |\n";
            }
            std::cout << std::flush;
        };

        // 1. Reference CsrBfs
        std::cerr << "  [Run] CsrBfs...\n";
        const double bfs_q_ns = measureNsPerQuery([&]{
            for (const auto& p : pairs) {
                sink = Bfs::reachable(csr, p.first, p.second, bfsScratch);
            }
        }, num_queries, 2);
        const double bfs_qps = 1e9 / bfs_q_ns;
        BenchResult res_bfs{"CsrBfs", "Baseline", d, 0, 0, 0.0, 0.0, bfs_q_ns, bfs_qps, 1.0, "OK"};
        printRow(res_bfs);

        // 2a. Oreach
        std::cerr << "  [Run] Oreach...\n";
        {
            GraphSearchScratch oreachScratch{V};
            OreachBaseline oreach;
            const double pre_ms = measureMs([&]{
                oreach.preprocess(csr, OreachBaselineParams{}, UINT64_MAX);
            });
            if (oreach.status() == BaselineStatus::Completed) {
                uint32_t oreach_correct = 0;
                uint32_t oreach_pos = 0;
                uint32_t settled_obs = 0;
                for (const auto& p : pairs) {
                    auto detailed = oreach.queryDetailed(p.first, p.second, oreachScratch);
                    auto oracle = Bfs::reachable(csr, p.first, p.second, bfsScratch);
                    if (detailed.answer == oracle) ++oreach_correct;
                    if (detailed.answer == ReachabilityAnswer::Reachable) ++oreach_pos;
                    if (detailed.settled_by_observation) ++settled_obs;
                }
                std::cerr << "  [Verify] Oreach agreement: " << oreach_correct << "/" << pairs.size()
                          << " (reported reachable: " << oreach_pos
                          << ", settled by observation: " << settled_obs << "/" << pairs.size() << ")\n";

                const double q_ns = measureNsPerQuery([&]{
                    for (const auto& p : pairs) {
                        sink = oreach.query(p.first, p.second, oreachScratch);
                    }
                }, num_queries, 4);
                const double qps = 1e9 / q_ns;
                const double mem_kib = static_cast<double>(oreach.indexStorageBytes()) / 1024.0;
                BenchResult res_o{"Oreach", "O-Reach DAG", d, 0, 0, pre_ms, mem_kib, q_ns, qps, bfs_q_ns / q_ns, "OK"};
                printRow(res_o);
            } else {
                BenchResult res_o{"Oreach", "O-Reach DAG", d, 0, 0, pre_ms, 0.0, 0.0, 0.0, 0.0, "Failed"};
                printRow(res_o);
            }
        }

        // 3. Sweeps across b and g
        for (uint32_t b : tile_sizes) {
            if (b > d) continue;

            for (uint32_t g : group_sizes) {
                std::string cfg_str = "b=" + std::to_string(b) + " g=" + std::to_string(g);

                // --- Normal H-BRICK ---
                std::cerr << "  [Run] HBrick (Normal) " << cfg_str << "...\n";
                {
                    HBrickConfig cfg;
                    cfg.base_tile_size = TileSize{b, b};
                    cfg.group_size = GroupSize{g, g};
                    cfg.max_depth = kHBrickFullDepth;
                    cfg.max_memory_bytes = 6ULL * 1024ULL * 1024ULL * 1024ULL;  // 6 GiB ledger cap
                    cfg.omit_local_closure = false;

                    try {
                        HBrickBaseline hb;
                        const double pre_ms = measureMs([&]{
                            hb.preprocess(grid, layout, cfg);
                        });

                        if (hb.status() == BaselineStatus::Completed) {
                            const double q_ns = measureNsPerQuery([&]{
                                for (const auto& p : pairs) {
                                    sink = hb.query(p.first, p.second);
                                }
                            }, num_queries, 2);
                            const double qps = 1e9 / q_ns;
                            const double mem_kib = static_cast<double>(hb.indexStorageBytes()) / 1024.0;
                            BenchResult r{"HBrick", cfg_str, d, b, g, pre_ms, mem_kib, q_ns, qps, bfs_q_ns / q_ns, "OK"};
                            printRow(r);

                            // --- H-BRICK with Fused Lifts ---
                            try {
                                HBrickFusedLiftBaseline hb_fused;
                                const double fused_pre_ms = measureMs([&]{
                                    hb_fused.adoptPrebuiltIndex(hb.index(), &grid);
                                });
                                const double fused_q_ns = measureNsPerQuery([&]{
                                    for (const auto& p : pairs) {
                                        sink = hb_fused.query(p.first, p.second);
                                    }
                                }, num_queries, 2);
                                const double fused_qps = 1e9 / fused_q_ns;
                                const double fused_mem_kib = static_cast<double>(hb_fused.indexStorageBytes()) / 1024.0;
                                BenchResult rf{"HBrickFusedLift", cfg_str, d, b, g, pre_ms + fused_pre_ms, fused_mem_kib, fused_q_ns, fused_qps, bfs_q_ns / fused_q_ns, "OK"};
                                printRow(rf);
                            } catch (...) {}

                            // --- H-BRICK with Fused Lifts + Endpoint Cache ---
                            try {
                                HBrickFusedLiftCacheBaseline hb_cached(64U);
                                const double cache_pre_ms = measureMs([&]{
                                    hb_cached.adoptPrebuiltIndex(hb.index(), &grid);
                                });
                                const double cache_q_ns = measureNsPerQuery([&]{
                                    for (const auto& p : pairs) {
                                        sink = hb_cached.query(p.first, p.second);
                                    }
                                }, num_queries, 2);
                                const double cache_qps = 1e9 / cache_q_ns;
                                const double cache_mem_kib = static_cast<double>(hb_cached.indexStorageBytes()) / 1024.0;
                                BenchResult rc{"HBrickFusedCache", cfg_str, d, b, g, pre_ms + cache_pre_ms, cache_mem_kib, cache_q_ns, cache_qps, bfs_q_ns / cache_q_ns, "OK"};
                                printRow(rc);
                            } catch (...) {}

                            // --- H-BRICK with skip-level fused lift products ---
                            try {
                                HBrickSkipLiftBaseline hb_skip;
                                const double skip_pre_ms = measureMs([&]{
                                    hb_skip.adoptPrebuiltIndex(hb.index(), &grid);
                                });
                                const double skip_q_ns = measureNsPerQuery([&]{
                                    for (const auto& p : pairs) {
                                        sink = hb_skip.query(p.first, p.second);
                                    }
                                }, num_queries, 2);
                                const double skip_qps = 1e9 / skip_q_ns;
                                const double skip_mem_kib = static_cast<double>(hb_skip.indexStorageBytes()) / 1024.0;
                                BenchResult rk{"HBrickSkipLift", cfg_str, d, b, g, pre_ms + skip_pre_ms, skip_mem_kib, skip_q_ns, skip_qps, bfs_q_ns / skip_q_ns, "OK"};
                                printRow(rk);
                            } catch (...) {}

                            // --- H-BRICK with SCC Condensed Ancestor Labels ---
                            try {
                                HBrickSccLabelBaseline hb_scc;
                                const double scc_pre_ms = measureMs([&]{
                                    hb_scc.adoptPrebuiltIndex(hb.index(), &grid);
                                });
                                const double scc_q_ns = measureNsPerQuery([&]{
                                    for (const auto& p : pairs) {
                                        sink = hb_scc.query(p.first, p.second);
                                    }
                                }, num_queries, 2);
                                const double scc_qps = 1e9 / scc_q_ns;
                                const double scc_mem_kib = static_cast<double>(hb_scc.indexStorageBytes()) / 1024.0;
                                BenchResult rs{"HBrickSccLabel", cfg_str, d, b, g, pre_ms + scc_pre_ms, scc_mem_kib, scc_q_ns, scc_qps, bfs_q_ns / scc_q_ns, "OK"};
                                printRow(rs);
                            } catch (...) {}
                        } else if (hb.status() == BaselineStatus::OutOfMemory) {
                            BenchResult r{"HBrick", cfg_str, d, b, g, pre_ms, 0.0, 0.0, 0.0, 0.0, "OutOfMemory (ledger exceeded)"};
                            printRow(r);
                        } else {
                            BenchResult r{"HBrick", cfg_str, d, b, g, pre_ms, 0.0, 0.0, 0.0, 0.0, "Failed"};
                            printRow(r);
                        }
                    } catch (const std::bad_alloc&) {
                        BenchResult r{"HBrick", cfg_str, d, b, g, 0.0, 0.0, 0.0, 0.0, 0.0, "OutOfMemory (bad_alloc)"};
                        printRow(r);
                    } catch (const std::exception& e) {
                        BenchResult r{"HBrick", cfg_str, d, b, g, 0.0, 0.0, 0.0, 0.0, 0.0, std::string("Error: ") + e.what()};
                        printRow(r);
                    }
                }

                // --- H-BRICK with Micro-BFS ---
                std::cerr << "  [Run] HBrickMicroBfs " << cfg_str << "...\n";
                {
                    HBrickConfig cfg;
                    cfg.base_tile_size = TileSize{b, b};
                    cfg.group_size = GroupSize{g, g};
                    cfg.max_depth = kHBrickFullDepth;
                    cfg.max_memory_bytes = 6ULL * 1024ULL * 1024ULL * 1024ULL;  // 6 GiB ledger cap
                    cfg.omit_local_closure = true;

                    try {
                        HBrickMicroBfsBaseline hb_micro;
                        const double pre_ms = measureMs([&]{
                            hb_micro.preprocess(grid, layout, cfg);
                        });

                        if (hb_micro.status() == BaselineStatus::Completed) {
                            const double q_ns = measureNsPerQuery([&]{
                                for (const auto& p : pairs) {
                                    sink = hb_micro.query(p.first, p.second);
                                }
                            }, num_queries, 2);
                            const double qps = 1e9 / q_ns;
                            const double mem_kib = static_cast<double>(hb_micro.indexStorageBytes()) / 1024.0;
                            BenchResult r{"HBrickMicroBfs", cfg_str, d, b, g, pre_ms, mem_kib, q_ns, qps, bfs_q_ns / q_ns, "OK"};
                            printRow(r);
                        } else if (hb_micro.status() == BaselineStatus::OutOfMemory) {
                            BenchResult r{"HBrickMicroBfs", cfg_str, d, b, g, pre_ms, 0.0, 0.0, 0.0, 0.0, "OutOfMemory (ledger exceeded)"};
                            printRow(r);
                        } else {
                            BenchResult r{"HBrickMicroBfs", cfg_str, d, b, g, pre_ms, 0.0, 0.0, 0.0, 0.0, "Failed"};
                            printRow(r);
                        }
                    } catch (const std::bad_alloc&) {
                        BenchResult r{"HBrickMicroBfs", cfg_str, d, b, g, 0.0, 0.0, 0.0, 0.0, 0.0, "OutOfMemory (bad_alloc)"};
                        printRow(r);
                    } catch (const std::exception& e) {
                        BenchResult r{"HBrickMicroBfs", cfg_str, d, b, g, 0.0, 0.0, 0.0, 0.0, 0.0, std::string("Error: ") + e.what()};
                        printRow(r);
                    }
                }
            }
        }
        std::cout << "\n";
    }

    return 0;
}
