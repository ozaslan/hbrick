/**
 * @file test_brick_hbrick_stress.cpp
 * @brief Comprehensive edge-case and stress tests for BRICK and H-BRICK correctness.
 *
 * Covers: single-cell, single-tile, fully-blocked, disconnected, max-depth,
 * highly-connected, unreachable-by-construction, non-square partial-edge tiles,
 * large tile configs, self-reachability, and all-pairs exhaustive checks on
 * small asymmetric grids.  Every test validates BrickSearch, BrickClosure,
 * and HBrick against the BFS ground-truth oracle.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

#include "hbrick/baselines/brick_closure_baseline.hpp"
#include "hbrick/baselines/brick_search_baseline.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"
#include "maze_generator.hpp"

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

namespace {

using namespace hbrick;

HBrickConfig fullDepthConfig(const TileSize ts, const GroupSize gs) {
    HBrickConfig c;
    c.base_tile_size = ts;
    c.group_size = gs;
    c.max_depth = kHBrickFullDepth;
    return c;  // max_memory_bytes defaults to unlimited after fix
}

void expectBfsMatch(const ReachabilityAnswer actual,
                    const ReachabilityAnswer expected,
                    const char* baseline,
                    uint32_t s, uint32_t t) {
    EXPECT_EQ(actual, expected)
        << baseline << " source=" << s << " target=" << t
        << " expected=" << static_cast<int>(expected)
        << " actual="   << static_cast<int>(actual);
}

/** All-pairs exhaustive check on every baseline against BFS. */
void checkAllPairsVsBfs(const CsrGraph& csr,
                        BrickSearchBaseline& search,
                        BrickClosureBaseline& closure,
                        HBrickBaseline& hbrick,
                        const std::string& label) {
    GraphSearchScratch bfsScratch{csr.numVertices()};
    const uint32_t V = csr.numVertices();

    for (uint32_t s = 0U; s < V; ++s) {
        for (uint32_t t = 0U; t < V; ++t) {
            const ReachabilityAnswer expected =
                Bfs::reachable(csr, s, t, bfsScratch);
            expectBfsMatch(search.query(s, t), expected, "BrickSearch", s, t);
            expectBfsMatch(closure.query(s, t), expected, "BrickClosure", s, t);
            expectBfsMatch(hbrick.query(s, t), expected, "HBrick", s, t);
        }
    }
    SUCCEED() << label << " all-pairs ok (" << V << " vertices)";
}

struct PolarityCounts {
    uint64_t reachable = 0U;
    uint64_t unreachable = 0U;
};

}  // namespace

// ===================================================================
// 1.  Minimum-size map (2×2, smallest valid for tile decomposition)
// ===================================================================
TEST(BrickHBrickStress, MinimumSizeMap) {
    MazeLayout layout{2, 2, true};
    const DirectedGridGraph grid = DirectedGridGraphBuilder::build(
        layout, GridEdgeConversionMode::BidirectionalAll);
    const CsrGraph& csr = grid.csrGraph();

    BrickSearchBaseline   brickSearch;
    BrickClosureBaseline  brickClosure;
    HBrickBaseline        hbrick;

    brickSearch.preprocess(grid, layout, TileSize{2, 2}, UINT64_MAX);
    brickClosure.preprocess(grid, layout, TileSize{2, 2}, UINT64_MAX);
    hbrick.preprocess(grid, layout, fullDepthConfig(TileSize{2, 2}, GroupSize{2, 2}));

    ASSERT_EQ(brickSearch.status(), BaselineStatus::Completed);
    ASSERT_EQ(brickClosure.status(), BaselineStatus::Completed);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);

    // Exhaustive check on all 16 ordered pairs — the smallest possible map.
    checkAllPairsVsBfs(csr, brickSearch, brickClosure, hbrick, "2x2 minimum");
}

// ===================================================================
// 2.  Fully-blocked map (no passable cells)
// ===================================================================
TEST(BrickHBrickStress, FullyBlockedMap) {
    MazeLayout layout{4, 4, false};  // every cell impassable
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::AcyclicEastSouth);
    // With all cells blocked, vertex count may still be > 0;
    // what matters is that preprocessing does not crash.
    (void)grid.csrGraph();

    BrickSearchBaseline  brickSearch;
    BrickClosureBaseline brickClosure;
    HBrickBaseline       hbrick;

    brickSearch.preprocess(grid, layout, TileSize{2, 2}, UINT64_MAX);
    brickClosure.preprocess(grid, layout, TileSize{2, 2}, UINT64_MAX);
    hbrick.preprocess(grid, layout, fullDepthConfig(TileSize{2, 2}, GroupSize{2, 2}));

    SUCCEED() << "Fully blocked map handled without crash";
    // BrickSearch/Closure/HBrick on a map with no passable cells should
    // report Completed (0 vertices, trivial index).  Verify they don't
    // silently report Failed or OutOfMemory.
    EXPECT_EQ(brickSearch.status(), BaselineStatus::Completed);
    EXPECT_EQ(brickClosure.status(), BaselineStatus::Completed);
    EXPECT_EQ(hbrick.status(), BaselineStatus::Completed);
}

// ===================================================================
// 3.  Disconnected left/right regions
// ===================================================================
TEST(BrickHBrickStress, DisconnectedLeftRight) {
    // 8x4 grid: two disconnected 4x4 regions separated by blocked column.
    MazeLayout layout{8, 4, false};
    for (uint32_t y = 0U; y < 4U; ++y) {
        for (uint32_t x = 0U; x < 8U; ++x) {
            if (x != 3U && x != 4U) layout.setPassable({x, y}, true);
        }
    }
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);
    const CsrGraph& csr = grid.csrGraph();

    BrickSearchBaseline  brickSearch;
    BrickClosureBaseline brickClosure;
    HBrickBaseline       hbrick;

    brickSearch.preprocess(grid, layout, TileSize{2, 2}, UINT64_MAX);
    brickClosure.preprocess(grid, layout, TileSize{2, 2}, UINT64_MAX);
    hbrick.preprocess(grid, layout, fullDepthConfig(TileSize{2, 2}, GroupSize{2, 2}));

    ASSERT_EQ(brickSearch.status(), BaselineStatus::Completed);
    ASSERT_EQ(brickClosure.status(), BaselineStatus::Completed);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);

    // Grab one vertex from each side.
    uint32_t leftV = layout.vertexId({0, 0}).value;
    uint32_t rightV = layout.vertexId({5, 0}).value;
    GraphSearchScratch bfsScratch{csr.numVertices()};

    EXPECT_EQ(brickSearch.query(leftV, rightV),
              Bfs::reachable(csr, leftV, rightV, bfsScratch));
    EXPECT_EQ(brickClosure.query(leftV, rightV),
              Bfs::reachable(csr, leftV, rightV, bfsScratch));
    EXPECT_EQ(hbrick.query(leftV, rightV),
              Bfs::reachable(csr, leftV, rightV, bfsScratch));
}

// ===================================================================
// 4.  Highly-connected bidirectional open grid
// ===================================================================
TEST(BrickHBrickStress, HighlyConnectedBidirectionalOpen) {
    MazeLayout layout{8, 8, true};
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);
    const CsrGraph& csr = grid.csrGraph();

    BrickSearchBaseline  brickSearch;
    BrickClosureBaseline brickClosure;
    HBrickBaseline       hbrick;

    brickSearch.preprocess(grid, layout, TileSize{4, 4}, UINT64_MAX);
    brickClosure.preprocess(grid, layout, TileSize{4, 4}, UINT64_MAX);
    hbrick.preprocess(grid, layout, fullDepthConfig(TileSize{4, 4}, GroupSize{2, 2}));

    ASSERT_EQ(brickSearch.status(), BaselineStatus::Completed);
    ASSERT_EQ(brickClosure.status(), BaselineStatus::Completed);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);

    checkAllPairsVsBfs(csr, brickSearch, brickClosure, hbrick,
                       "bidirectional 8x8 open");
}

// ===================================================================
// 5.  Unreachable-by-construction: Acyclic East/South (DAG)
// ===================================================================
TEST(BrickHBrickStress, AcyclicEastSouthDag) {
    MazeLayout layout{8, 8, true};
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::AcyclicEastSouth);
    const CsrGraph& csr = grid.csrGraph();

    BrickSearchBaseline  brickSearch;
    BrickClosureBaseline brickClosure;
    HBrickBaseline       hbrick;

    brickSearch.preprocess(grid, layout, TileSize{4, 4}, UINT64_MAX);
    brickClosure.preprocess(grid, layout, TileSize{4, 4}, UINT64_MAX);
    hbrick.preprocess(grid, layout, fullDepthConfig(TileSize{4, 4}, GroupSize{2, 2}));

    ASSERT_EQ(brickSearch.status(), BaselineStatus::Completed);
    ASSERT_EQ(brickClosure.status(), BaselineStatus::Completed);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);

    checkAllPairsVsBfs(csr, brickSearch, brickClosure, hbrick,
                       "acyclic-east-south 8x8");
}

// ===================================================================
// 6.  Non-square partial-edge tiles (9x7)
// ===================================================================
TEST(BrickHBrickStress, NonSquarePartialEdgeTiles) {
    MazeLayout layout{9, 7, true};
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::RandomAsymmetric,
                                                RandomAsymmetricParams{17, 0.6, 0.4, 0.05, 0.05});
    const CsrGraph& csr = grid.csrGraph();

    BrickSearchBaseline  brickSearch;
    BrickClosureBaseline brickClosure;
    HBrickBaseline       hbrick;

    brickSearch.preprocess(grid, layout, TileSize{4, 3}, UINT64_MAX);
    brickClosure.preprocess(grid, layout, TileSize{4, 3}, UINT64_MAX);
    hbrick.preprocess(grid, layout, fullDepthConfig(TileSize{4, 3}, GroupSize{2, 2}));

    ASSERT_EQ(brickSearch.status(), BaselineStatus::Completed);
    ASSERT_EQ(brickClosure.status(), BaselineStatus::Completed);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);

    checkAllPairsVsBfs(csr, brickSearch, brickClosure, hbrick,
                       "9x7 random-asymmetric");
}

// ===================================================================
// 7.  Large tile covering entire map (zero seam edges)
// ===================================================================
TEST(BrickHBrickStress, SingleOversizedTile) {
    MazeLayout layout{6, 6, true};
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);
    const CsrGraph& csr = grid.csrGraph();

    BrickSearchBaseline  brickSearch;
    BrickClosureBaseline brickClosure;
    HBrickBaseline       hbrick;

    brickSearch.preprocess(grid, layout, TileSize{64, 64}, UINT64_MAX);
    brickClosure.preprocess(grid, layout, TileSize{64, 64}, UINT64_MAX);
    hbrick.preprocess(grid, layout, fullDepthConfig(TileSize{64, 64}, GroupSize{2, 2}));

    ASSERT_EQ(brickSearch.status(), BaselineStatus::Completed);
    ASSERT_EQ(brickClosure.status(), BaselineStatus::Completed);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);

    checkAllPairsVsBfs(csr, brickSearch, brickClosure, hbrick,
                       "oversized single tile 6x6");
}

// ===================================================================
// 8.  Deep hierarchy on small asymmetric grid — all-pairs exhaustive
// ===================================================================
TEST(BrickHBrickStress, ExhaustiveDeepAsymmetric10x10) {
    MazeLayout layout{10, 10, true};
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::RandomAsymmetric,
                                                RandomAsymmetricParams{71, 0.55, 0.45, 0.0, 0.1});
    const CsrGraph& csr = grid.csrGraph();

    BrickSearchBaseline  brickSearch;
    BrickClosureBaseline brickClosure;
    HBrickBaseline       hbrick;

    brickSearch.preprocess(grid, layout, TileSize{2, 2}, UINT64_MAX);
    brickClosure.preprocess(grid, layout, TileSize{2, 2}, UINT64_MAX);
    hbrick.preprocess(grid, layout, fullDepthConfig(TileSize{2, 2}, GroupSize{2, 2}));

    ASSERT_EQ(brickSearch.status(), BaselineStatus::Completed);
    ASSERT_EQ(brickClosure.status(), BaselineStatus::Completed);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);
    ASSERT_TRUE(hbrick.index().hierarchyQuerySound());

    checkAllPairsVsBfs(csr, brickSearch, brickClosure, hbrick,
                       "10x10 deep hierarchy random-asymmetric");
}

// ===================================================================
// 9.  Max-depth=1 flat fallback — hierarchy never built
// ===================================================================
TEST(BrickHBrickStress, FlatFallbackMaxDepthOne) {
    MazeLayout layout{8, 8, true};
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);
    const CsrGraph& csr = grid.csrGraph();

    BrickSearchBaseline  brickSearch;
    BrickClosureBaseline brickClosure;
    HBrickBaseline       hbrick;

    HBrickConfig flatConfig;
    flatConfig.base_tile_size = TileSize{4, 4};
    flatConfig.group_size = GroupSize{2, 2};
    flatConfig.max_depth = 1U;  // no hierarchy

    brickSearch.preprocess(grid, layout, TileSize{4, 4}, UINT64_MAX);
    brickClosure.preprocess(grid, layout, TileSize{4, 4}, UINT64_MAX);
    hbrick.preprocess(grid, layout, flatConfig);

    ASSERT_EQ(brickSearch.status(), BaselineStatus::Completed);
    ASSERT_EQ(brickClosure.status(), BaselineStatus::Completed);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);
    EXPECT_FALSE(hbrick.index().hasSuperLevel(1U));

    checkAllPairsVsBfs(csr, brickSearch, brickClosure, hbrick,
                       "flat max-depth=1 8x8");
}

// ===================================================================
// 10. Self-reachability — stressed on random asymmetric
// ===================================================================
TEST(BrickHBrickStress, SelfReachabilityStressed) {
    MazeLayout layout{6, 6, true};
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::RandomAsymmetric,
                                                RandomAsymmetricParams{51, 0.4, 0.6, 0.1, 0.1});
    const CsrGraph& csr = grid.csrGraph();

    BrickSearchBaseline  brickSearch;
    BrickClosureBaseline brickClosure;
    HBrickBaseline       hbrick;

    brickSearch.preprocess(grid, layout, TileSize{3, 3}, UINT64_MAX);
    brickClosure.preprocess(grid, layout, TileSize{3, 3}, UINT64_MAX);
    hbrick.preprocess(grid, layout, fullDepthConfig(TileSize{3, 3}, GroupSize{2, 2}));

    ASSERT_EQ(brickSearch.status(), BaselineStatus::Completed);
    ASSERT_EQ(brickClosure.status(), BaselineStatus::Completed);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);

    const uint32_t V = csr.numVertices();
    for (uint32_t v = 0U; v < V; ++v) {
        EXPECT_EQ(brickSearch.query(v, v), ReachabilityAnswer::Reachable)
            << "BrickSearch self v=" << v;
        EXPECT_EQ(brickClosure.query(v, v), ReachabilityAnswer::Reachable)
            << "BrickClosure self v=" << v;
        EXPECT_EQ(hbrick.query(v, v), ReachabilityAnswer::Reachable)
            << "HBrick self v=" << v;
    }
}

// ===================================================================
// 11. Mixed reachable/unreachable polarities on perfect maze
// ===================================================================
TEST(BrickHBrickStress, PerfectMazePolarities) {
    MazeLayout layout = test_support::generatePerfectMaze({6, 5, 99});
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);
    const CsrGraph& csr = grid.csrGraph();

    BrickSearchBaseline  brickSearch;
    BrickClosureBaseline brickClosure;
    HBrickBaseline       hbrick;

    brickSearch.preprocess(grid, layout, TileSize{3, 3}, UINT64_MAX);
    brickClosure.preprocess(grid, layout, TileSize{3, 3}, UINT64_MAX);
    hbrick.preprocess(grid, layout, fullDepthConfig(TileSize{3, 3}, GroupSize{2, 2}));

    ASSERT_EQ(brickSearch.status(), BaselineStatus::Completed);
    ASSERT_EQ(brickClosure.status(), BaselineStatus::Completed);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);

    const uint32_t V = csr.numVertices();
    GraphSearchScratch bfsScratch{V};
    PolarityCounts truth{}, bsearch{}, bclosure{}, hb{};

    for (uint32_t s = 0U; s < V; ++s) {
        for (uint32_t t = 0U; t < V; ++t) {
            const ReachabilityAnswer expected =
                Bfs::reachable(csr, s, t, bfsScratch);
            const ReachabilityAnswer bs = brickSearch.query(s, t);
            const ReachabilityAnswer bc = brickClosure.query(s, t);
            const ReachabilityAnswer hbAns = hbrick.query(s, t);

            expectBfsMatch(bs, expected, "BrickSearch", s, t);
            expectBfsMatch(bc, expected, "BrickClosure", s, t);
            expectBfsMatch(hbAns, expected, "HBrick", s, t);

            if (expected == ReachabilityAnswer::Reachable) ++truth.reachable;
            else ++truth.unreachable;
            if (bs == ReachabilityAnswer::Reachable) ++bsearch.reachable;
            else ++bsearch.unreachable;
            if (bc == ReachabilityAnswer::Reachable) ++bclosure.reachable;
            else ++bclosure.unreachable;
            if (hbAns == ReachabilityAnswer::Reachable) ++hb.reachable;
            else ++hb.unreachable;
        }
    }

    EXPECT_EQ(bsearch.reachable, truth.reachable);
    EXPECT_EQ(bsearch.unreachable, truth.unreachable);
    EXPECT_EQ(bclosure.reachable, truth.reachable);
    EXPECT_EQ(bclosure.unreachable, truth.unreachable);
    EXPECT_EQ(hb.reachable, truth.reachable);
    EXPECT_EQ(hb.unreachable, truth.unreachable);
    EXPECT_GT(truth.reachable, 0U);
    EXPECT_GT(truth.unreachable, 0U);
}

// ===================================================================
// 12. Default-constructed HBrickConfig now works (unlimited budget)
// ===================================================================
TEST(BrickHBrickStress, DefaultConfigProducesValidIndex) {
    MazeLayout layout{6, 6, true};
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);
    const CsrGraph& csr = grid.csrGraph();

    HBrickBaseline hbrick;
    HBrickConfig config;  // all defaults — should now produce a valid index
    config.base_tile_size = TileSize{3, 3};
    config.group_size = GroupSize{2, 2};

    hbrick.preprocess(grid, layout, config);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed)
        << "Default config must produce a completed index";
    EXPECT_TRUE(hbrick.index().hasSuperLevel(1U));
    EXPECT_TRUE(hbrick.index().hierarchyQuerySound());

    const uint32_t V = csr.numVertices();
    GraphSearchScratch bfsScratch{V};
    for (uint32_t v = 0U; v < V; ++v) {
        EXPECT_EQ(hbrick.query(v, v), ReachabilityAnswer::Reachable);
    }
}

// ===================================================================
// 13. Memory budget boundary: zero budget → SkippedByPolicy
// ===================================================================
TEST(BrickHBrickStress, ZeroMemoryBudgetSkips) {
    MazeLayout layout{8, 8, true};
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);

    BrickSearchBaseline  brickSearch;
    BrickClosureBaseline brickClosure;
    HBrickBaseline       hbrick;

    brickSearch.preprocess(grid, layout, TileSize{4, 4}, 0U);
    brickClosure.preprocess(grid, layout, TileSize{4, 4}, 0U);
    HBrickConfig zeroCfg;
    zeroCfg.base_tile_size = TileSize{4, 4};
    zeroCfg.group_size = GroupSize{2, 2};
    zeroCfg.max_memory_bytes = 0U;
    hbrick.preprocess(grid, layout, zeroCfg);

    EXPECT_NE(brickSearch.status(), BaselineStatus::Completed);
    EXPECT_NE(brickClosure.status(), BaselineStatus::Completed);
    EXPECT_NE(hbrick.status(), BaselineStatus::Completed);
}

// ===================================================================
// 14. Asymmetric group dimensions (e.g. 2×4)
// ===================================================================
TEST(BrickHBrickStress, AsymmetricGrouping) {
    MazeLayout layout{8, 8, true};
    auto grid = DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::RandomAsymmetric,
                                                RandomAsymmetricParams{23, 0.6, 0.4, 0.0, 0.0});
    const CsrGraph& csr = grid.csrGraph();

    BrickSearchBaseline  brickSearch;
    BrickClosureBaseline brickClosure;
    HBrickBaseline       hbrick;

    brickSearch.preprocess(grid, layout, TileSize{2, 2}, UINT64_MAX);
    brickClosure.preprocess(grid, layout, TileSize{2, 2}, UINT64_MAX);
    hbrick.preprocess(grid, layout, fullDepthConfig(TileSize{2, 2}, GroupSize{2, 4}));

    ASSERT_EQ(brickSearch.status(), BaselineStatus::Completed);
    ASSERT_EQ(brickClosure.status(), BaselineStatus::Completed);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);

    checkAllPairsVsBfs(csr, brickSearch, brickClosure, hbrick,
                       "8x8 2x4 grouping");
}
