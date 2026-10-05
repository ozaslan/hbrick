/**
 * @file test_hbrick_query_bench.cpp
 * @brief Quantitative query-throughput microbenchmark: BRICK vs H-BRICK vs O'Reach vs BFS.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "hbrick/baselines/brick_closure_baseline.hpp"
#include "hbrick/baselines/brick_search_baseline.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/baselines/oreach_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/io/movingai_loader.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

using namespace hbrick;
using Clock = std::chrono::steady_clock;

struct Row {
    std::string name;
    double pre_ms = 0.0;
    double idx_kib = 0.0;
    double q_ns = 0.0;
    double speedup = 1.0;
};

template <typename F> double nsAvg(F&& fn, uint64_t n) {
    const auto t0 = Clock::now();
    for (uint64_t i = 0; i < n; ++i) {
        fn();
    }
    return static_cast<double>((Clock::now() - t0).count()) / static_cast<double>(n);
}

template <typename F> double msDuration(F&& fn) {
    const auto t0 = Clock::now();
    fn();
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void print(const char* title, double connectivity_pct, const std::vector<Row>& rows) {
    std::cout << "\n=== " << title;
    if (connectivity_pct >= 0.0) {
        std::cout << " (Reachability Connectivity: " << std::fixed << std::setprecision(2)
                  << connectivity_pct << "%)";
    }
    std::cout << " ===\n"
              << "Method          | Pre(ms) | IdxKiB | Qry(ns) | Speedup vs BFS\n"
              << "----------------|---------|--------|---------|---------------\n";
    for (const auto& r : rows) {
        std::cout << std::left  << std::setw(16) << r.name << "| "
                  << std::right << std::setw(7)  << std::fixed
                  << std::setprecision(2) << r.pre_ms << " | "
                  << std::setw(6)  << std::setprecision(0) << r.idx_kib << " | "
                  << std::setw(7)  << std::setprecision(1) << r.q_ns << " | "
                  << std::setw(7)  << std::setprecision(2) << r.speedup << "x\n";
    }
}

}  // namespace

TEST(QueryBench, Bidirectional16x16) {
    MazeLayout layout{16, 16, true};
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);
    const CsrGraph& csr = grid.csrGraph();
    const uint32_t V = csr.numVertices();
    constexpr uint32_t N = 4096U;

    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    pairs.reserve(N);
    for (uint32_t i = 0; i < N; ++i) {
        pairs.emplace_back((i * 9973U + 17U) % V, (i * 6271U + 31U) % V);
    }

    volatile ReachabilityAnswer sink{ReachabilityAnswer::Unreachable};
    GraphSearchScratch bfsScratch{V};
    std::vector<Row> rows;

    const double bfs_q = nsAvg([&]{
        for (const auto& p : pairs) sink = Bfs::reachable(csr, p.first, p.second, bfsScratch);
    }, 1) / N;
    rows.push_back({"CsrBfs", 0.0, 0.0, bfs_q, 1.0});

    {
        BrickSearchBaseline bs;
        const double pre = msDuration([&]{ bs.preprocess(grid, layout, TileSize{4, 4}, UINT64_MAX); });
        ASSERT_EQ(bs.status(), BaselineStatus::Completed);
        const double q = nsAvg([&]{ for (const auto& p : pairs) sink = bs.query(p.first, p.second); }, 1) / N;
        rows.push_back({"BrickSearch", pre, double(bs.indexStorageBytes()) / 1024.0, q, bfs_q / q});
    }

    {
        BrickClosureBaseline bc;
        const double pre = msDuration([&]{ bc.preprocess(grid, layout, TileSize{4, 4}, UINT64_MAX); });
        ASSERT_EQ(bc.status(), BaselineStatus::Completed);
        const double q = nsAvg([&]{ for (const auto& p : pairs) sink = bc.query(p.first, p.second); }, 1) / N;
        rows.push_back({"BrickClosure", pre, double(bc.indexStorageBytes()) / 1024.0, q, bfs_q / q});
    }

    {
        HBrickConfig cfg;
        cfg.base_tile_size = TileSize{4, 4};
        cfg.group_size = GroupSize{2, 2};
        cfg.max_depth = kHBrickFullDepth;
        HBrickBaseline hb;
        const double pre = msDuration([&]{ hb.preprocess(grid, layout, cfg); });
        ASSERT_EQ(hb.status(), BaselineStatus::Completed);
        const double q = nsAvg([&]{ for (const auto& p : pairs) sink = hb.query(p.first, p.second); }, 1) / N;
        rows.push_back({"HBrick", pre, double(hb.indexStorageBytes()) / 1024.0, q, bfs_q / q});
    }

    print("16x16 bidirectional 4x4 tiles 2x2 groups", 100.0, rows);
    EXPECT_GE(rows.size(), 4U);
}

TEST(QueryBench, LowConnectivity128x128) {
    MazeLayout layout{128, 128, true};
    auto grid = DirectedGridGraphBuilder::build(
        layout,
        GridEdgeConversionMode::RandomAsymmetric,
        RandomAsymmetricParams{42ULL, 0.40L, 0.05L, 45.0, 0.01L}
    );
    const CsrGraph& csr = grid.csrGraph();
    const uint32_t V = csr.numVertices();
    constexpr uint32_t N = 4096U;

    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    pairs.reserve(N);
    for (uint32_t i = 0; i < N; ++i) {
        pairs.emplace_back((i * 9973U + 17U) % V, (i * 6271U + 31U) % V);
    }

    volatile ReachabilityAnswer sink{ReachabilityAnswer::Unreachable};
    GraphSearchScratch bfsScratch{V};
    std::vector<Row> rows;

    uint32_t reachable_count = 0;
    for (const auto& p : pairs) {
        if (Bfs::reachable(csr, p.first, p.second, bfsScratch) == ReachabilityAnswer::Reachable) {
            ++reachable_count;
        }
    }
    const double conn_pct = (100.0 * reachable_count) / static_cast<double>(pairs.size());

    const double bfs_q = nsAvg([&]{
        for (const auto& p : pairs) sink = Bfs::reachable(csr, p.first, p.second, bfsScratch);
    }, 2) / N;
    rows.push_back({"CsrBfs", 0.0, 0.0, bfs_q, 1.0});

    {
        GraphSearchScratch oreachScratch{V};
        OreachBaseline oreach;
        const double pre = msDuration([&]{ oreach.preprocess(csr, OreachBaselineParams{}, UINT64_MAX); });
        ASSERT_EQ(oreach.status(), BaselineStatus::Completed);
        const double q = nsAvg([&]{ for (const auto& p : pairs) sink = oreach.query(p.first, p.second, oreachScratch); }, 2) / N;
        rows.push_back({"Oreach", pre, double(oreach.indexStorageBytes()) / 1024.0, q, bfs_q / q});
    }

    auto testHBrick = [&](const char* name, TileSize tile, GroupSize group, uint32_t depth) {
        HBrickConfig cfg;
        cfg.base_tile_size = tile;
        cfg.group_size = group;
        cfg.max_depth = depth;
        HBrickBaseline hb;
        const double pre = msDuration([&]{ hb.preprocess(grid, layout, cfg); });
        ASSERT_EQ(hb.status(), BaselineStatus::Completed);
        const double q = nsAvg([&]{ for (const auto& p : pairs) sink = hb.query(p.first, p.second); }, 2) / N;
        rows.push_back({name, pre, double(hb.indexStorageBytes()) / 1024.0, q, bfs_q / q});
    };

    testHBrick("HBrick_t8_g2", TileSize{8, 8}, GroupSize{2, 2}, kHBrickFullDepth);
    testHBrick("HBrick_t16_g2", TileSize{16, 16}, GroupSize{2, 2}, kHBrickFullDepth);
    testHBrick("HBrick_t32_g2", TileSize{32, 32}, GroupSize{2, 2}, kHBrickFullDepth);
    testHBrick("HBrick_t32_g4", TileSize{32, 32}, GroupSize{4, 4}, kHBrickFullDepth);
    testHBrick("HBrick_t64_g2", TileSize{64, 64}, GroupSize{2, 2}, kHBrickFullDepth);

    print("128x128 Low-Connectivity Directed Grid", conn_pct, rows);
    EXPECT_LT(conn_pct, 5.0);
}

TEST(QueryBench, LowConnectivity256x256) {
    MazeLayout layout{256, 256, true};
    auto grid = DirectedGridGraphBuilder::build(
        layout,
        GridEdgeConversionMode::RandomAsymmetric,
        RandomAsymmetricParams{123ULL, 0.35L, 0.05L, 45.0, 0.01L}
    );
    const CsrGraph& csr = grid.csrGraph();
    const uint32_t V = csr.numVertices();
    constexpr uint32_t N = 2048U;

    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    pairs.reserve(N);
    for (uint32_t i = 0; i < N; ++i) {
        pairs.emplace_back((i * 9973U + 17U) % V, (i * 6271U + 31U) % V);
    }

    volatile ReachabilityAnswer sink{ReachabilityAnswer::Unreachable};
    GraphSearchScratch bfsScratch{V};
    std::vector<Row> rows;

    uint32_t reachable_count = 0;
    for (const auto& p : pairs) {
        if (Bfs::reachable(csr, p.first, p.second, bfsScratch) == ReachabilityAnswer::Reachable) {
            ++reachable_count;
        }
    }
    const double conn_pct = (100.0 * reachable_count) / static_cast<double>(pairs.size());

    const double bfs_q = nsAvg([&]{
        for (const auto& p : pairs) sink = Bfs::reachable(csr, p.first, p.second, bfsScratch);
    }, 1) / N;
    rows.push_back({"CsrBfs", 0.0, 0.0, bfs_q, 1.0});

    {
        GraphSearchScratch oreachScratch{V};
        OreachBaseline oreach;
        const double pre = msDuration([&]{ oreach.preprocess(csr, OreachBaselineParams{}, UINT64_MAX); });
        ASSERT_EQ(oreach.status(), BaselineStatus::Completed);
        const double q = nsAvg([&]{ for (const auto& p : pairs) sink = oreach.query(p.first, p.second, oreachScratch); }, 2) / N;
        rows.push_back({"Oreach", pre, double(oreach.indexStorageBytes()) / 1024.0, q, bfs_q / q});
    }

    auto testHBrick = [&](const char* name, TileSize tile, GroupSize group, uint32_t depth) {
        HBrickConfig cfg;
        cfg.base_tile_size = tile;
        cfg.group_size = group;
        cfg.max_depth = depth;
        HBrickBaseline hb;
        const double pre = msDuration([&]{ hb.preprocess(grid, layout, cfg); });
        ASSERT_EQ(hb.status(), BaselineStatus::Completed);
        const double q = nsAvg([&]{ for (const auto& p : pairs) sink = hb.query(p.first, p.second); }, 2) / N;
        rows.push_back({name, pre, double(hb.indexStorageBytes()) / 1024.0, q, bfs_q / q});
    };

    testHBrick("HBrick_t16_g2", TileSize{16, 16}, GroupSize{2, 2}, kHBrickFullDepth);
    testHBrick("HBrick_t32_g2", TileSize{32, 32}, GroupSize{2, 2}, kHBrickFullDepth);
    testHBrick("HBrick_t32_g4", TileSize{32, 32}, GroupSize{4, 4}, kHBrickFullDepth);
    testHBrick("HBrick_t64_g2", TileSize{64, 64}, GroupSize{2, 2}, kHBrickFullDepth);
    testHBrick("HBrick_t64_g4", TileSize{64, 64}, GroupSize{4, 4}, kHBrickFullDepth);

    print("256x256 Low-Connectivity Directed Grid", conn_pct, rows);
    EXPECT_LT(conn_pct, 5.0);
}

TEST(QueryBench, LowConnectivityMovingAiMaze512) {
    const std::filesystem::path map_path =
        std::filesystem::path(HBRICK_SOURCE_DIR) / "datasets" / "movingai"
        / "maze" / "maps" / "maze512-1-0.map";
    if (!std::filesystem::exists(map_path)) {
        GTEST_SKIP() << "Dataset map not found: " << map_path;
    }
    const MovingAiLoadResult load_res = loadMovingAiMap(map_path);
    ASSERT_TRUE(load_res.ok()) << load_res.error;
    const MazeLayout layout = load_res.map.toMazeLayout(MovingAiPassabilityPolicy::GroundOnly);
    auto grid = DirectedGridGraphBuilder::build(
        layout,
        GridEdgeConversionMode::RandomAsymmetric,
        RandomAsymmetricParams{0x1dd08d5fe133867cULL, 0.22L, 0.12L, 45.0, 0.02L}
    );
    const CsrGraph& csr = grid.csrGraph();
    const uint32_t V = csr.numVertices();
    constexpr uint32_t N = 2048U;

    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    pairs.reserve(N);
    for (uint32_t i = 0; i < N; ++i) {
        pairs.emplace_back((i * 9973U + 17U) % V, (i * 6271U + 31U) % V);
    }

    volatile ReachabilityAnswer sink{ReachabilityAnswer::Unreachable};
    GraphSearchScratch bfsScratch{V};
    std::vector<Row> rows;

    uint32_t reachable_count = 0;
    for (const auto& p : pairs) {
        if (Bfs::reachable(csr, p.first, p.second, bfsScratch) == ReachabilityAnswer::Reachable) {
            ++reachable_count;
        }
    }
    const double conn_pct = (100.0 * reachable_count) / static_cast<double>(pairs.size());

    const double bfs_q = nsAvg([&]{
        for (const auto& p : pairs) sink = Bfs::reachable(csr, p.first, p.second, bfsScratch);
    }, 1) / N;
    rows.push_back({"CsrBfs", 0.0, 0.0, bfs_q, 1.0});

    {
        GraphSearchScratch oreachScratch{V};
        OreachBaseline oreach;
        const double pre = msDuration([&]{ oreach.preprocess(csr, OreachBaselineParams{}, UINT64_MAX); });
        ASSERT_EQ(oreach.status(), BaselineStatus::Completed);
        const double q = nsAvg([&]{ for (const auto& p : pairs) sink = oreach.query(p.first, p.second, oreachScratch); }, 2) / N;
        rows.push_back({"Oreach", pre, double(oreach.indexStorageBytes()) / 1024.0, q, bfs_q / q});
    }

    auto testHBrick = [&](const char* name, TileSize tile, GroupSize group, uint32_t depth) {
        HBrickConfig cfg;
        cfg.base_tile_size = tile;
        cfg.group_size = group;
        cfg.max_depth = depth;
        HBrickBaseline hb;
        const double pre = msDuration([&]{ hb.preprocess(grid, layout, cfg); });
        ASSERT_EQ(hb.status(), BaselineStatus::Completed);
        const double q = nsAvg([&]{ for (const auto& p : pairs) sink = hb.query(p.first, p.second); }, 2) / N;
        rows.push_back({name, pre, double(hb.indexStorageBytes()) / 1024.0, q, bfs_q / q});
    };

    testHBrick("HBrick_t16_g2", TileSize{16, 16}, GroupSize{2, 2}, kHBrickFullDepth);
    testHBrick("HBrick_t32_g2", TileSize{32, 32}, GroupSize{2, 2}, kHBrickFullDepth);
    testHBrick("HBrick_t32_g4", TileSize{32, 32}, GroupSize{4, 4}, kHBrickFullDepth);
    testHBrick("HBrick_t64_g2", TileSize{64, 64}, GroupSize{2, 2}, kHBrickFullDepth);
    testHBrick("HBrick_t64_g4", TileSize{64, 64}, GroupSize{4, 4}, kHBrickFullDepth);

    print("512x512 maze512-1-0 MovingAI Benchmark", conn_pct, rows);
    EXPECT_LT(conn_pct, 5.0);
}

TEST(QueryBench, LowConnectivity512x512) {
    MazeLayout layout{512, 512, true};
    auto grid = DirectedGridGraphBuilder::build(
        layout,
        GridEdgeConversionMode::RandomAsymmetric,
        RandomAsymmetricParams{777ULL, 0.35L, 0.05L, 45.0, 0.01L}
    );
    const CsrGraph& csr = grid.csrGraph();
    const uint32_t V = csr.numVertices();
    constexpr uint32_t N = 1024U;

    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    pairs.reserve(N);
    for (uint32_t i = 0; i < N; ++i) {
        pairs.emplace_back((i * 9973U + 17U) % V, (i * 6271U + 31U) % V);
    }

    volatile ReachabilityAnswer sink{ReachabilityAnswer::Unreachable};
    GraphSearchScratch bfsScratch{V};
    std::vector<Row> rows;

    uint32_t reachable_count = 0;
    for (const auto& p : pairs) {
        if (Bfs::reachable(csr, p.first, p.second, bfsScratch) == ReachabilityAnswer::Reachable) {
            ++reachable_count;
        }
    }
    const double conn_pct = (100.0 * reachable_count) / static_cast<double>(pairs.size());

    const double bfs_q = nsAvg([&]{
        for (const auto& p : pairs) sink = Bfs::reachable(csr, p.first, p.second, bfsScratch);
    }, 1) / N;
    rows.push_back({"CsrBfs", 0.0, 0.0, bfs_q, 1.0});

    {
        GraphSearchScratch oreachScratch{V};
        OreachBaseline oreach;
        const double pre = msDuration([&]{ oreach.preprocess(csr, OreachBaselineParams{}, UINT64_MAX); });
        ASSERT_EQ(oreach.status(), BaselineStatus::Completed);
        const double q = nsAvg([&]{ for (const auto& p : pairs) sink = oreach.query(p.first, p.second, oreachScratch); }, 2) / N;
        rows.push_back({"Oreach", pre, double(oreach.indexStorageBytes()) / 1024.0, q, bfs_q / q});
    }

    auto testHBrick = [&](const char* name, TileSize tile, GroupSize group, uint32_t depth) {
        HBrickConfig cfg;
        cfg.base_tile_size = tile;
        cfg.group_size = group;
        cfg.max_depth = depth;
        HBrickBaseline hb;
        const double pre = msDuration([&]{ hb.preprocess(grid, layout, cfg); });
        ASSERT_EQ(hb.status(), BaselineStatus::Completed);
        const double q = nsAvg([&]{ for (const auto& p : pairs) sink = hb.query(p.first, p.second); }, 2) / N;
        rows.push_back({name, pre, double(hb.indexStorageBytes()) / 1024.0, q, bfs_q / q});
    };

    testHBrick("HBrick_t16_g2", TileSize{16, 16}, GroupSize{2, 2}, kHBrickFullDepth);
    testHBrick("HBrick_t32_g2", TileSize{32, 32}, GroupSize{2, 2}, kHBrickFullDepth);
    testHBrick("HBrick_t32_g4", TileSize{32, 32}, GroupSize{4, 4}, kHBrickFullDepth);
    testHBrick("HBrick_t64_g2", TileSize{64, 64}, GroupSize{2, 2}, kHBrickFullDepth);
    testHBrick("HBrick_t64_g4", TileSize{64, 64}, GroupSize{4, 4}, kHBrickFullDepth);

    print("512x512 Low-Connectivity Directed Grid", conn_pct, rows);
    EXPECT_LT(conn_pct, 5.0);
}

