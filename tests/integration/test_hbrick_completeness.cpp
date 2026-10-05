/**
 * @file test_hbrick_completeness.cpp
 * @brief Integration tests for H-BRICK hierarchy soundness and MovingAI coverage.
 *
 * 1. Verifies that when the hierarchy is incomplete (truncated depth),
 *    hierarchyQuerySound() is false and public query still matches BFS
 *    (flat port-BFS fallback). Complements
 *    BrickHBrickExhaustive.TruncatedDepthStillCorrectViaFallback with an
 *    explicit unsoundness assertion on a larger map.
 * 2. Runs the shared sampled-pair reachability oracle on a small MovingAI
 *    map with an explicit full-depth HBrickConfig (the catalog MovingAI
 *    suite already covers H-BRICK via the same oracle; this pins a
 *    dedicated H-BRICK-focused entry point).
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

#include "hbrick/baselines/hbrick_baseline.hpp"
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
#include "movingai_map_catalog.hpp"
#include "reachability_oracle.hpp"
#include "test_limits.hpp"

namespace {

using namespace hbrick;

// ---------------------------------------------------------------------------
// 1.  Truncated-depth hierarchy: hierarchyQuerySound() == false
// ---------------------------------------------------------------------------
TEST(HBrickCompleteness, TruncatedDepthHierarchyNotSound) {
    // 16×16 map, 2×2 tiles (8×8 tile grid), 2×2 groups.
    // Full depth reaches a single root; truncating at depth=2 leaves
    // multiple roots → hierarchyQuerySound() is false → queries must fall
    // back to flat port BFS for Unreachable answers.
    MazeLayout layout{16, 16, true};
    const DirectedGridGraph grid = DirectedGridGraphBuilder::build(
        layout, GridEdgeConversionMode::RandomAsymmetric,
        RandomAsymmetricParams{71, 0.55, 0.45, 0.0, 0.1});
    const CsrGraph& csr = grid.csrGraph();

    HBrickConfig cfg;
    cfg.base_tile_size = TileSize{2, 2};
    cfg.group_size = GroupSize{2, 2};
    cfg.max_depth = 2U;  // truncated — does not reach single root

    HBrickBaseline hbrick;
    hbrick.preprocess(grid, layout, cfg);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);

    // Hierarchy is not sound — multiple roots at the top level.
    EXPECT_TRUE(hbrick.index().hasSuperLevel(1U));
    EXPECT_FALSE(hbrick.index().hierarchyQuerySound());

    // All-pairs check: truncated hierarchy + BFS fallback must still match.
    const uint32_t V = csr.numVertices();
    GraphSearchScratch bfs_scratch{V};
    uint64_t unreachable_pairs = 0U;
    for (uint32_t s = 0U; s < V; ++s) {
        for (uint32_t t = 0U; t < V; ++t) {
            const ReachabilityAnswer truth =
                Bfs::reachable(csr, s, t, bfs_scratch);
            if (truth == ReachabilityAnswer::Unreachable) {
                ++unreachable_pairs;
            }
            EXPECT_EQ(hbrick.query(s, t), truth)
                << "Truncated H-BRICK s=" << s << " t=" << t;
        }
    }
    // Ensure the Unreachable / fallback path is exercised on this fixture.
    EXPECT_GT(unreachable_pairs, 0U);
}

// ---------------------------------------------------------------------------
// 2.  H-BRICK on a small MovingAI map
// ---------------------------------------------------------------------------
[[nodiscard]] std::filesystem::path datasetsRoot() {
    return std::filesystem::path(HBRICK_SOURCE_DIR)
        / "datasets" / "movingai";
}

TEST(HBrickCompleteness, SmallMovingAiMapWithHbrick) {
    // Use a genuinely small DAO map so the sampled oracle runs the BRICK /
    // H-BRICK slice (V must be ≤ kSampledOracleFullCheckVertexLimit).
    // lak303d is far too large for that path and would silently skip H-BRICK.
    const hbrick::test_support::MovingAiMapEntry entry{"dao", "ost102d.map"};
    const std::filesystem::path map_path =
        datasetsRoot() / entry.set_name / "maps" / entry.map_name;

    if (!std::filesystem::exists(map_path)) {
        GTEST_SKIP() << "Missing movingai map: " << map_path.string();
    }

    const MovingAiLoadResult loaded = loadMovingAiMap(map_path);
    ASSERT_TRUE(loaded.ok()) << entry.label();

    const MovingAiPassabilityPolicy policy =
        hbrick::test_support::passabilityPolicyForMovingAiSet(entry.set_name);
    const MazeLayout layout = loaded.map.toMazeLayout(policy);
    const DirectedGridGraph grid = DirectedGridGraphBuilder::build(
        layout,
        GridEdgeConversionMode::RandomAsymmetric,
        hbrick::test_support::randomParamsForMovingAiMap(
            entry.set_name, entry.map_name)
    );
    const CsrGraph& csr = grid.csrGraph();
    ASSERT_LE(
        csr.numVertices(),
        hbrick::test_support::kSampledOracleFullCheckVertexLimit
    ) << "fixture must be small enough for BRICK/H-BRICK oracle slices";

    HBrickConfig cfg;
    cfg.base_tile_size = TileSize{4, 4};
    cfg.group_size = GroupSize{2, 2};
    cfg.max_depth = kHBrickFullDepth;

    HBrickBaseline hbrick;
    hbrick.preprocess(grid, layout, cfg);
    ASSERT_EQ(hbrick.status(), BaselineStatus::Completed);
    // Irregular MovingAI dimensions need not collapse to a single root;
    // public query correctness is checked by the oracle below (which falls
    // back when hierarchyQuerySound() is false).
    EXPECT_TRUE(hbrick.index().hasSuperLevel(1U));

    // Sampled-pair oracle (includes search / BRICK / H-BRICK slices).
    hbrick::test_support::expectReachabilityOracleSampledPairs(
        csr, entry.label(),
        hbrick::test_support::kIntegrationReachabilitySamplePairCount,
        std::numeric_limits<uint64_t>::max(),
        &layout,
        TileSize{4, 4},
        &cfg
    );
}

}  // namespace
