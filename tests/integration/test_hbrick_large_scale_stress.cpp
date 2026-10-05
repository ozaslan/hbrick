/**
 * @file test_hbrick_large_scale_stress.cpp
 * @brief Large multi-component exhaustive validation: BRICK & H-BRICK vs BFS & DFS.
 *
 * Builds a 64×64 dungeon with a few dozen spatially connected passable rooms,
 * orients edges so within-component directed reachability averages 15–25%,
 * then exhaustively compares BrickSearch, BrickClosure, and H-BRICK against
 * both BFS and DFS oracles on all passable ordered pairs.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <iostream>
#include <limits>
#include <queue>
#include <vector>

#include "hbrick/baselines/brick_closure_baseline.hpp"
#include "hbrick/baselines/brick_search_baseline.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/core/vertex_id.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/dfs.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

using namespace hbrick;

constexpr uint32_t kMapW = 64U;
constexpr uint32_t kMapH = 64U;
constexpr uint32_t kRoomW = 5U;
constexpr uint32_t kRoomH = 5U;
constexpr uint32_t kPitchX = 7U;  // 5 passable + 2 wall
constexpr uint32_t kPitchY = 7U;
constexpr uint32_t kRoomsX = 8U;
constexpr uint32_t kRoomsY = 6U;
constexpr uint64_t kOrientationSeed = 0xC0FFEE42ULL;

/**
 * @brief 64×64 dungeon: rectangular rooms separated by walls, sparse corridors.
 *
 * Corridor cells sit in the wall gaps (not on room edges). Opening a minority of
 * candidate seams yields a few dozen remaining spatial components.
 */
MazeLayout buildDungeonLayout() {
    MazeLayout layout{kMapW, kMapH, false};

    for (uint32_t ry = 0U; ry < kRoomsY; ++ry) {
        for (uint32_t rx = 0U; rx < kRoomsX; ++rx) {
            const uint32_t ox = 1U + rx * kPitchX;
            const uint32_t oy = 1U + ry * kPitchY;
            for (uint32_t y = oy; y < oy + kRoomH; ++y) {
                for (uint32_t x = ox; x < ox + kRoomW; ++x) {
                    layout.setPassable({x, y}, true);
                }
            }
        }
    }

    for (uint32_t ry = 0U; ry < kRoomsY; ++ry) {
        for (uint32_t rx = 0U; rx + 1U < kRoomsX; ++rx) {
            if ((rx + 3U * ry) % 7U != 0U) {
                continue;
            }
            const uint32_t gap_x = 1U + rx * kPitchX + kRoomW;
            const uint32_t cy = 1U + ry * kPitchY + kRoomH / 2U;
            layout.setPassable({gap_x, cy}, true);
            layout.setPassable({gap_x + 1U, cy}, true);
        }
    }

    for (uint32_t ry = 0U; ry + 1U < kRoomsY; ++ry) {
        for (uint32_t rx = 0U; rx < kRoomsX; ++rx) {
            if ((ry + 2U * rx) % 7U != 1U) {
                continue;
            }
            const uint32_t gap_y = 1U + ry * kPitchY + kRoomH;
            const uint32_t cx = 1U + rx * kPitchX + kRoomW / 2U;
            layout.setPassable({cx, gap_y}, true);
            layout.setPassable({cx, gap_y + 1U}, true);
        }
    }

    return layout;
}

struct ComponentStats {
    uint32_t passable_vertices = 0U;
    uint32_t spatial_components = 0U;
    double within_component_reach_pct = 0.0;
    uint64_t within_pairs = 0U;
    uint64_t within_reachable = 0U;
    std::vector<uint32_t> passable;
    std::vector<std::vector<uint32_t>> members;
};

/**
 * @brief Labels passable 4-connected spatial components (ignores edge orientation).
 *
 * Sparse RandomAsymmetric orientations intentionally omit many adjacencies, so
 * digraph weak-connectivity is the wrong notion of "room / component" here.
 */
[[nodiscard]] uint32_t labelSpatialComponents(
    const MazeLayout& layout,
    std::vector<uint32_t>& labels
) {
    const uint32_t V = kMapW * kMapH;
    labels.assign(V, std::numeric_limits<uint32_t>::max());
    uint32_t num_components = 0U;

    auto vertex_at = [](uint32_t x, uint32_t y) noexcept -> uint32_t {
        return y * kMapW + x;
    };

    constexpr int kDx[4] = {1, -1, 0, 0};
    constexpr int kDy[4] = {0, 0, 1, -1};

    for (uint32_t y = 0U; y < kMapH; ++y) {
        for (uint32_t x = 0U; x < kMapW; ++x) {
            const uint32_t start = vertex_at(x, y);
            if (!layout.isPassable({x, y})
                || labels[start] != std::numeric_limits<uint32_t>::max()) {
                continue;
            }

            std::queue<uint32_t> frontier;
            frontier.push(start);
            labels[start] = num_components;
            while (!frontier.empty()) {
                const uint32_t u = frontier.front();
                frontier.pop();
                const uint32_t ux = u % kMapW;
                const uint32_t uy = u / kMapW;
                for (int dir = 0; dir < 4; ++dir) {
                    const int nx = static_cast<int>(ux) + kDx[dir];
                    const int ny = static_cast<int>(uy) + kDy[dir];
                    if (nx < 0 || ny < 0
                        || nx >= static_cast<int>(kMapW)
                        || ny >= static_cast<int>(kMapH)) {
                        continue;
                    }
                    const uint32_t nv =
                        vertex_at(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
                    if (!layout.isPassable(
                            {static_cast<uint32_t>(nx), static_cast<uint32_t>(ny)}
                        )
                        || labels[nv] != std::numeric_limits<uint32_t>::max()) {
                        continue;
                    }
                    labels[nv] = num_components;
                    frontier.push(nv);
                }
            }
            ++num_components;
        }
    }
    return num_components;
}

[[nodiscard]] ComponentStats measureComponentStats(
    const MazeLayout& layout,
    const CsrGraph& csr
) {
    ComponentStats stats;
    std::vector<uint32_t> labels;
    stats.spatial_components = labelSpatialComponents(layout, labels);

    const uint32_t V = csr.numVertices();
    stats.passable.reserve(V);
    for (uint32_t v = 0U; v < V; ++v) {
        if (layout.isPassable(VertexId{v})) {
            stats.passable.push_back(v);
        }
    }
    stats.passable_vertices = static_cast<uint32_t>(stats.passable.size());
    stats.members.assign(stats.spatial_components, {});
    for (const uint32_t v : stats.passable) {
        stats.members[labels[v]].push_back(v);
    }

    GraphSearchScratch scratch{V};
    for (const std::vector<uint32_t>& component : stats.members) {
        for (const uint32_t s : component) {
            for (const uint32_t t : component) {
                ++stats.within_pairs;
                if (Bfs::reachable(csr, s, t, scratch) == ReachabilityAnswer::Reachable) {
                    ++stats.within_reachable;
                }
            }
        }
    }

    stats.within_component_reach_pct =
        stats.within_pairs == 0U
            ? 0.0
            : 100.0 * static_cast<double>(stats.within_reachable)
                / static_cast<double>(stats.within_pairs);
    return stats;
}

[[nodiscard]] DirectedGridGraph buildOrientedGrid(const MazeLayout& layout) {
    // Tuned offline: ~36 spatial CCs and ~17% within-component directed reachability.
    return DirectedGridGraphBuilder::build(
        layout,
        GridEdgeConversionMode::RandomAsymmetric,
        RandomAsymmetricParams{kOrientationSeed, 0.35L, 0.15L, 0.0, 0.0}
    );
}

}  // namespace

TEST(LargeScaleStress, FixtureMeetsComponentAndDensityTargets) {
    const MazeLayout layout = buildDungeonLayout();
    const DirectedGridGraph grid = buildOrientedGrid(layout);
    const ComponentStats stats = measureComponentStats(layout, grid.csrGraph());

    std::cerr << "[large-scale fixture] passable=" << stats.passable_vertices
              << " spatial_ccs=" << stats.spatial_components
              << " within_reach=" << stats.within_reachable << "/" << stats.within_pairs
              << " (" << stats.within_component_reach_pct << "%)"
              << std::endl;

    EXPECT_GE(stats.passable_vertices, 800U);
    EXPECT_GE(stats.spatial_components, 20U);
    EXPECT_LE(stats.spatial_components, 40U);
    EXPECT_GE(stats.within_component_reach_pct, 15.0);
    EXPECT_LE(stats.within_component_reach_pct, 25.0);
}

TEST(LargeScaleStress, Dungeon64x64ExhaustiveVsBfsDfs) {
    const MazeLayout layout = buildDungeonLayout();
    const DirectedGridGraph grid = buildOrientedGrid(layout);
    const CsrGraph& csr = grid.csrGraph();
    const uint32_t V = csr.numVertices();
    ASSERT_EQ(V, kMapW * kMapH);

    const ComponentStats stats = measureComponentStats(layout, csr);
    std::cerr << "[large-scale] map=" << kMapW << "x" << kMapH
              << " passable=" << stats.passable_vertices
              << " spatial_ccs=" << stats.spatial_components
              << " within_reach=" << stats.within_reachable << "/" << stats.within_pairs
              << " (" << stats.within_component_reach_pct << "%)"
              << std::endl;

    ASSERT_GE(stats.spatial_components, 20U);
    ASSERT_LE(stats.spatial_components, 40U);
    ASSERT_GE(stats.within_component_reach_pct, 15.0);
    ASSERT_LE(stats.within_component_reach_pct, 25.0);

    BrickSearchBaseline bs;
    BrickClosureBaseline bc;
    HBrickBaseline hb;
    bs.preprocess(grid, layout, TileSize{8, 8}, UINT64_MAX);
    bc.preprocess(grid, layout, TileSize{8, 8}, UINT64_MAX);
    HBrickConfig cfg;
    cfg.base_tile_size = TileSize{8, 8};
    cfg.group_size = GroupSize{2, 2};
    cfg.max_depth = kHBrickFullDepth;
    hb.preprocess(grid, layout, cfg);

    ASSERT_EQ(bs.status(), BaselineStatus::Completed);
    ASSERT_EQ(bc.status(), BaselineStatus::Completed);
    ASSERT_EQ(hb.status(), BaselineStatus::Completed);

    GraphSearchScratch bfs_scratch{V};
    GraphSearchScratch dfs_scratch{V};
    uint64_t total = 0U;
    uint64_t reachable = 0U;
    uint64_t bs_mismatch = 0U;
    uint64_t bc_mismatch = 0U;
    uint64_t hb_mismatch = 0U;

    for (const uint32_t s : stats.passable) {
        for (const uint32_t t : stats.passable) {
            ++total;
            const ReachabilityAnswer bfs_truth = Bfs::reachable(csr, s, t, bfs_scratch);
            const ReachabilityAnswer dfs_truth = Dfs::reachable(csr, s, t, dfs_scratch);
            ASSERT_EQ(bfs_truth, dfs_truth) << "BFS/DFS disagree s=" << s << " t=" << t;

            if (bfs_truth == ReachabilityAnswer::Reachable) {
                ++reachable;
            }

            const ReachabilityAnswer sr = bs.query(s, t);
            const ReachabilityAnswer cr = bc.query(s, t);
            const ReachabilityAnswer hr = hb.query(s, t);
            if (sr != bfs_truth) {
                ++bs_mismatch;
                EXPECT_EQ(sr, bfs_truth) << "BrickSearch s=" << s << " t=" << t;
            }
            if (cr != bfs_truth) {
                ++bc_mismatch;
                EXPECT_EQ(cr, bfs_truth) << "BrickClosure s=" << s << " t=" << t;
            }
            if (hr != bfs_truth) {
                ++hb_mismatch;
                EXPECT_EQ(hr, bfs_truth) << "HBrick s=" << s << " t=" << t;
            }
        }
    }

    const double global_pct =
        100.0 * static_cast<double>(reachable) / static_cast<double>(total);
    std::cerr << "[large-scale] exhaustive passable pairs=" << total
              << " reachable=" << reachable << " (" << global_pct << "%)"
              << "\n  BrickSearch  ok=" << (total - bs_mismatch)
              << " mismatch=" << bs_mismatch
              << "\n  BrickClosure ok=" << (total - bc_mismatch)
              << " mismatch=" << bc_mismatch
              << "\n  HBrick       ok=" << (total - hb_mismatch)
              << " mismatch=" << hb_mismatch
              << std::endl;

    EXPECT_EQ(bs_mismatch, 0U);
    EXPECT_EQ(bc_mismatch, 0U);
    EXPECT_EQ(hb_mismatch, 0U);
    EXPECT_GT(reachable, 0U);
    EXPECT_GT(total - reachable, 0U);
}
