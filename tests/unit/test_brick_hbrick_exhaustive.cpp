#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "hbrick/baselines/brick_closure_baseline.hpp"
#include "hbrick/baselines/brick_search_baseline.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"
#include "maze_generator.hpp"

namespace {

struct PolarityCounts {
    uint64_t reachable = 0U;
    uint64_t unreachable = 0U;
};

hbrick::HBrickConfig fullDepthConfig(
    const hbrick::TileSize base_tile_size,
    const hbrick::GroupSize group_size
) {
    hbrick::HBrickConfig config{};
    config.base_tile_size = base_tile_size;
    config.group_size = group_size;
    config.max_depth = hbrick::kHBrickFullDepth;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();
    return config;
}

void expectExactMatch(
    const hbrick::ReachabilityAnswer actual,
    const hbrick::ReachabilityAnswer expected,
    const std::string& context,
    const uint32_t source,
    const uint32_t target
) {
    EXPECT_EQ(actual, expected)
        << context << " source=" << source << " target=" << target
        << " expected="
        << (expected == hbrick::ReachabilityAnswer::Reachable ? "Reachable"
                                                              : "Unreachable")
        << " actual="
        << (actual == hbrick::ReachabilityAnswer::Reachable ? "Reachable"
                                                            : "Unreachable");
}

/**
 * Fail-hard all-pairs oracle for BrickSearch, BrickClosure, and HBrick.
 *
 * Deliberately checks both polarities: a buggy index that always answers
 * Reachable (or always Unreachable) must fail when the map has both kinds
 * of pairs.
 */
void expectBrickAndHBrickExactAllPairs(
    const hbrick::MazeLayout& layout,
    const hbrick::DirectedGridGraph& graph,
    const hbrick::TileSize brick_tile_size,
    const hbrick::HBrickConfig& hbrick_config,
    const std::string& context,
    const bool require_hierarchy_sound
) {
    hbrick::BrickSearchBaseline brick_search;
    brick_search.preprocess(
        graph,
        layout,
        brick_tile_size,
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(brick_search.status(), hbrick::BaselineStatus::Completed) << context;

    hbrick::BrickClosureBaseline brick_closure;
    brick_closure.preprocess(
        graph,
        layout,
        brick_tile_size,
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(brick_closure.status(), hbrick::BaselineStatus::Completed) << context;

    hbrick::HBrickBaseline hbrick;
    hbrick.preprocess(graph, layout, hbrick_config);
    ASSERT_EQ(hbrick.status(), hbrick::BaselineStatus::Completed) << context;
    if (require_hierarchy_sound) {
        ASSERT_TRUE(hbrick.index().hierarchyQuerySound())
            << context << " expected single-root completed hierarchy";
    }

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    const hbrick::CsrGraph& csr = graph.csrGraph();
    PolarityCounts truth{};
    PolarityCounts brick_search_polarity{};
    PolarityCounts brick_closure_polarity{};
    PolarityCounts hbrick_polarity{};

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer expected =
                hbrick::Bfs::reachable(csr, source, target, bfs_scratch);
            if (expected == hbrick::ReachabilityAnswer::Reachable) {
                ++truth.reachable;
            } else {
                ++truth.unreachable;
            }

            const hbrick::ReachabilityAnswer search_answer =
                brick_search.query(source, target);
            expectExactMatch(
                search_answer,
                expected,
                context + " BrickSearch",
                source,
                target
            );
            if (search_answer == hbrick::ReachabilityAnswer::Reachable) {
                ++brick_search_polarity.reachable;
            } else {
                ++brick_search_polarity.unreachable;
            }

            const hbrick::ReachabilityAnswer closure_answer =
                brick_closure.query(source, target);
            expectExactMatch(
                closure_answer,
                expected,
                context + " BrickClosure",
                source,
                target
            );
            if (closure_answer == hbrick::ReachabilityAnswer::Reachable) {
                ++brick_closure_polarity.reachable;
            } else {
                ++brick_closure_polarity.unreachable;
            }

            const hbrick::ReachabilityAnswer hbrick_answer =
                hbrick.query(source, target);
            expectExactMatch(
                hbrick_answer,
                expected,
                context + " HBrick",
                source,
                target
            );
            if (hbrick_answer == hbrick::ReachabilityAnswer::Reachable) {
                ++hbrick_polarity.reachable;
            } else {
                ++hbrick_polarity.unreachable;
            }
        }
    }

    ASSERT_EQ(brick_search_polarity.reachable, truth.reachable) << context;
    ASSERT_EQ(brick_search_polarity.unreachable, truth.unreachable) << context;
    ASSERT_EQ(brick_closure_polarity.reachable, truth.reachable) << context;
    ASSERT_EQ(brick_closure_polarity.unreachable, truth.unreachable) << context;
    ASSERT_EQ(hbrick_polarity.reachable, truth.reachable) << context;
    ASSERT_EQ(hbrick_polarity.unreachable, truth.unreachable) << context;

    // Guard against maps that are too trivial for this adversarial suite.
    ASSERT_GT(truth.reachable, 0U) << context << " need positive reachability pairs";
    ASSERT_GT(truth.unreachable, 0U)
        << context << " need negative reachability pairs to catch always-Reachable bugs";
}

hbrick::DirectedGridGraph buildReentryGraph(hbrick::MazeLayout& layout) {
    constexpr uint32_t kWidth = 8U;
    constexpr uint32_t kHeight = 4U;
    layout = hbrick::MazeLayout{kWidth, kHeight, true};
    hbrick::CsrGraphBuilder builder{kWidth * kHeight};

    const auto vertex = [](const uint32_t x, const uint32_t y) {
        return y * 8U + x;
    };
    const auto edge = [&](const uint32_t fx, const uint32_t fy, const uint32_t tx,
                          const uint32_t ty) {
        builder.addEdge(vertex(fx, fy), vertex(tx, ty));
    };

    // Leaves tile 0, travels through tile 1, re-enters tile 0.
    edge(0U, 2U, 0U, 1U);
    edge(0U, 1U, 1U, 1U);
    edge(1U, 1U, 2U, 1U);
    edge(2U, 1U, 3U, 1U);
    edge(3U, 1U, 4U, 1U);
    edge(4U, 1U, 5U, 1U);
    edge(5U, 1U, 6U, 1U);
    edge(6U, 1U, 7U, 1U);
    edge(7U, 1U, 7U, 2U);
    edge(7U, 2U, 6U, 2U);
    edge(6U, 2U, 5U, 2U);
    edge(5U, 2U, 4U, 2U);
    edge(4U, 2U, 3U, 2U);

    return hbrick::DirectedGridGraph::fromCsr(kWidth, kHeight, builder.build());
}

}  // namespace

TEST(BrickHBrickExhaustive, SameTileReentryRequiresLeavingTile) {
    hbrick::MazeLayout layout(8U, 4U, true);
    const hbrick::DirectedGridGraph graph = buildReentryGraph(layout);
    expectBrickAndHBrickExactAllPairs(
        layout,
        graph,
        hbrick::TileSize{4U, 4U},
        fullDepthConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}),
        "same-tile-reentry",
        true
    );
}

TEST(BrickHBrickExhaustive, AsymmetricEightByEightMultipleConfigs) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{2U, 2U}, false);
    layout.setPassable(hbrick::GridCoord{5U, 5U}, false);
    layout.setPassable(hbrick::GridCoord{1U, 6U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{31U, 0.55, 0.45, 0.05, 0.05}
    );

    const hbrick::TileSize tiles[] = {
        hbrick::TileSize{2U, 2U},
        hbrick::TileSize{4U, 4U},
    };
    const hbrick::GroupSize groups[] = {
        hbrick::GroupSize{2U, 2U},
        hbrick::GroupSize{2U, 4U},
    };

    for (const hbrick::TileSize tile_size : tiles) {
        for (const hbrick::GroupSize group_size : groups) {
            expectBrickAndHBrickExactAllPairs(
                layout,
                graph,
                tile_size,
                fullDepthConfig(tile_size, group_size),
                "8x8-asymmetric-t" + std::to_string(tile_size.width) + "x"
                    + std::to_string(tile_size.height) + "-g"
                    + std::to_string(group_size.group_w) + "x"
                    + std::to_string(group_size.group_h),
                true
            );
        }
    }
}

TEST(BrickHBrickExhaustive, PartialEdgeTilesNineBySeven) {
    hbrick::MazeLayout layout(9U, 7U);
    layout.setPassable(hbrick::GridCoord{3U, 3U}, false);
    layout.setPassable(hbrick::GridCoord{7U, 1U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{17U, 0.6, 0.4, 0.0, 0.0}
    );

    expectBrickAndHBrickExactAllPairs(
        layout,
        graph,
        hbrick::TileSize{4U, 4U},
        fullDepthConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}),
        "9x7-partial-edge",
        true
    );
}

TEST(BrickHBrickExhaustive, DeepHierarchySixteenBySixteen) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    layout.setPassable(hbrick::GridCoord{11U, 6U}, false);
    layout.setPassable(hbrick::GridCoord{7U, 12U}, false);
    layout.setPassable(hbrick::GridCoord{14U, 14U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{41U, 0.6, 0.4, 0.05, 0.05}
    );

    expectBrickAndHBrickExactAllPairs(
        layout,
        graph,
        hbrick::TileSize{2U, 2U},
        fullDepthConfig(hbrick::TileSize{2U, 2U}, hbrick::GroupSize{2U, 2U}),
        "16x16-deep-hierarchy",
        true
    );
}

TEST(BrickHBrickExhaustive, BidirectionalGridWithObstacles) {
    // Obstacles break strong connectivity so the suite still has Unreachable
    // pairs (guards always-Reachable bugs).
    hbrick::MazeLayout layout(12U, 12U, true);
    layout.setPassable(hbrick::GridCoord{3U, 3U}, false);
    layout.setPassable(hbrick::GridCoord{8U, 8U}, false);
    layout.setPassable(hbrick::GridCoord{5U, 9U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    expectBrickAndHBrickExactAllPairs(
        layout,
        graph,
        hbrick::TileSize{4U, 4U},
        fullDepthConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}),
        "12x12-bidirectional-obstacles",
        true
    );
}

TEST(BrickHBrickExhaustive, DisconnectedComponents) {
    hbrick::MazeLayout layout(10U, 10U, false);
    for (uint32_t y = 1U; y <= 3U; ++y) {
        for (uint32_t x = 1U; x <= 3U; ++x) {
            layout.setPassable(hbrick::GridCoord{x, y}, true);
        }
    }
    for (uint32_t y = 6U; y <= 8U; ++y) {
        for (uint32_t x = 6U; x <= 8U; ++x) {
            layout.setPassable(hbrick::GridCoord{x, y}, true);
        }
    }
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{7U, 0.7, 0.3, 0.0, 0.0}
    );

    expectBrickAndHBrickExactAllPairs(
        layout,
        graph,
        hbrick::TileSize{4U, 4U},
        fullDepthConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}),
        "disconnected-components",
        true
    );
}

TEST(BrickHBrickExhaustive, PerfectMazeAndCyclicMaze) {
    const hbrick::test_support::MazeParams params{6U, 5U, 99U};
    const hbrick::MazeLayout perfect =
        hbrick::test_support::generatePerfectMaze(params);
    const hbrick::DirectedGridGraph perfect_graph =
        hbrick::DirectedGridGraphBuilder::build(
            perfect,
            hbrick::GridEdgeConversionMode::BidirectionalAll
        );
    expectBrickAndHBrickExactAllPairs(
        perfect,
        perfect_graph,
        hbrick::TileSize{4U, 4U},
        fullDepthConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}),
        "perfect-maze",
        true
    );

    const hbrick::MazeLayout cyclic =
        hbrick::test_support::generateMazeWithExtraPassages(params, 123U, 8U);
    const hbrick::DirectedGridGraph cyclic_graph =
        hbrick::DirectedGridGraphBuilder::build(
            cyclic,
            hbrick::GridEdgeConversionMode::RandomAsymmetric,
            hbrick::RandomAsymmetricParams{123U, 0.55, 0.45, 0.1, 0.1}
        );
    expectBrickAndHBrickExactAllPairs(
        cyclic,
        cyclic_graph,
        hbrick::TileSize{4U, 4U},
        fullDepthConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}),
        "cyclic-maze",
        true
    );
}

TEST(BrickHBrickExhaustive, TruncatedDepthStillCorrectViaFallback) {
    hbrick::MazeLayout layout(8U, 8U, true);
    layout.setPassable(hbrick::GridCoord{3U, 3U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{19U, 0.5, 0.5, 0.0, 0.0}
    );

    hbrick::HBrickConfig truncated = fullDepthConfig(
        hbrick::TileSize{2U, 2U},
        hbrick::GroupSize{2U, 2U}
    );
    truncated.max_depth = 2U;  // may not reach a single root on 8x8 with 2x2 tiles

    hbrick::HBrickBaseline hbrick;
    hbrick.preprocess(graph, layout, truncated);
    ASSERT_EQ(hbrick.status(), hbrick::BaselineStatus::Completed);

    // Truncated trees are allowed to be unsound hierarchically; public query must
    // still match BFS via flat fallback when needed.
    expectBrickAndHBrickExactAllPairs(
        layout,
        graph,
        hbrick::TileSize{2U, 2U},
        truncated,
        "truncated-depth-fallback",
        false
    );
}

TEST(BrickHBrickExhaustive, EmptyInteriorSingleTileNoPorts) {
    // Fully walled perimeter: one big tile, no boundary ports. Same-tile local
    // closure must still answer interior reachability correctly.
    hbrick::MazeLayout layout(12U, 12U, false);
    for (uint32_t y = 1U; y + 1U < 12U; ++y) {
        for (uint32_t x = 1U; x + 1U < 12U; ++x) {
            layout.setPassable(hbrick::GridCoord{x, y}, true);
        }
    }
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::BrickSearchBaseline brick_search;
    brick_search.preprocess(
        graph,
        layout,
        hbrick::TileSize{64U, 64U},
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(brick_search.status(), hbrick::BaselineStatus::Completed);
    EXPECT_EQ(brick_search.index().ports().numPorts(), 0U);

    hbrick::HBrickBaseline hbrick;
    hbrick.preprocess(
        graph,
        layout,
        fullDepthConfig(hbrick::TileSize{64U, 64U}, hbrick::GroupSize{2U, 2U})
    );
    ASSERT_EQ(hbrick.status(), hbrick::BaselineStatus::Completed);

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    const hbrick::CsrGraph& csr = graph.csrGraph();
    uint64_t reachable = 0U;
    uint64_t unreachable = 0U;
    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer expected =
                hbrick::Bfs::reachable(csr, source, target, bfs_scratch);
            expectExactMatch(
                brick_search.query(source, target),
                expected,
                "empty-ports BrickSearch",
                source,
                target
            );
            expectExactMatch(
                hbrick.query(source, target),
                expected,
                "empty-ports HBrick",
                source,
                target
            );
            if (expected == hbrick::ReachabilityAnswer::Reachable) {
                ++reachable;
            } else {
                ++unreachable;
            }
        }
    }
    ASSERT_GT(reachable, 0U);
    ASSERT_GT(unreachable, 0U);
}

TEST(BrickHBrickExhaustive, OneWayBottleneckValveAcrossTileSeam) {
    // 12x4 map: Room 1 [0..4]x[0..3], Bridge (5..6, 1), Room 2 [7..11]x[0..3]
    // All tiles inside Room 1 and Room 2 are bidirectional.
    // The corridor (4,1) -> (5,1) -> (6,1) -> (7,1) is strictly one-way East.
    // Crosses a 4x4 tile boundary at x=4 and x=8.
    const uint32_t width = 12U;
    const uint32_t height = 4U;
    hbrick::MazeLayout layout(width, height, false);

    // Make rooms and bridge passable
    for (uint32_t y = 0U; y < height; ++y) {
        for (uint32_t x = 0U; x <= 4U; ++x) {
            layout.setPassable(hbrick::GridCoord{x, y}, true);
        }
        for (uint32_t x = 7U; x < width; ++x) {
            layout.setPassable(hbrick::GridCoord{x, y}, true);
        }
    }
    layout.setPassable(hbrick::GridCoord{5U, 1U}, true);
    layout.setPassable(hbrick::GridCoord{6U, 1U}, true);

    hbrick::CsrGraphBuilder builder{width * height};
    const auto coord_to_id = [width](uint32_t x, uint32_t y) { return y * width + x; };

    // Connect Room 1 bidirectionally
    for (uint32_t y = 0U; y < height; ++y) {
        for (uint32_t x = 0U; x <= 4U; ++x) {
            const uint32_t u = coord_to_id(x, y);
            if (x + 1U <= 4U) {
                const uint32_t v = coord_to_id(x + 1U, y);
                builder.addEdge(u, v);
                builder.addEdge(v, u);
            }
            if (y + 1U < height) {
                const uint32_t v = coord_to_id(x, y + 1U);
                builder.addEdge(u, v);
                builder.addEdge(v, u);
            }
        }
    }

    // Connect Room 2 bidirectionally
    for (uint32_t y = 0U; y < height; ++y) {
        for (uint32_t x = 7U; x < width; ++x) {
            const uint32_t u = coord_to_id(x, y);
            if (x + 1U < width) {
                const uint32_t v = coord_to_id(x + 1U, y);
                builder.addEdge(u, v);
                builder.addEdge(v, u);
            }
            if (y + 1U < height) {
                const uint32_t v = coord_to_id(x, y + 1U);
                builder.addEdge(u, v);
                builder.addEdge(v, u);
            }
        }
    }

    // Connect bridge with STRICTLY ONE-WAY East edges
    builder.addEdge(coord_to_id(4U, 1U), coord_to_id(5U, 1U));
    builder.addEdge(coord_to_id(5U, 1U), coord_to_id(6U, 1U));
    builder.addEdge(coord_to_id(6U, 1U), coord_to_id(7U, 1U));

    const hbrick::DirectedGridGraph graph =
        hbrick::DirectedGridGraph::fromCsr(width, height, builder.build());

    expectBrickAndHBrickExactAllPairs(
        layout,
        graph,
        hbrick::TileSize{4U, 4U},
        fullDepthConfig(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}),
        "one-way-bottleneck-valve",
        true
    );
}

TEST(BrickHBrickExhaustive, WindingSnakeLabyrinth) {
    // 9x9 winding snake maze: physical neighbors (0, 0) and (0, 2) are separated
    // by a long path of 18 steps that winds back and forth across 3x3 tiles.
    const uint32_t size = 9U;
    hbrick::MazeLayout layout(size, size, false);

    for (uint32_t y = 0U; y < size; y += 2U) {
        for (uint32_t x = 0U; x < size; ++x) {
            layout.setPassable(hbrick::GridCoord{x, y}, true);
        }
    }
    // Vertical connectors at alternate ends
    layout.setPassable(hbrick::GridCoord{size - 1U, 1U}, true);
    layout.setPassable(hbrick::GridCoord{0U, 3U}, true);
    layout.setPassable(hbrick::GridCoord{size - 1U, 5U}, true);
    layout.setPassable(hbrick::GridCoord{0U, 7U}, true);

    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    expectBrickAndHBrickExactAllPairs(
        layout,
        graph,
        hbrick::TileSize{3U, 3U},
        fullDepthConfig(hbrick::TileSize{3U, 3U}, hbrick::GroupSize{2U, 2U}),
        "winding-snake-9x9",
        true
    );
}

TEST(BrickHBrickExhaustive, SizeLadderFromSmallToMedium) {
    // Tests a continuous ladder of sizes: 3x3, 5x5, 7x7, 11x11, 15x15
    // with both Bidirectional and RandomAsymmetric edge modes.
    const std::vector<uint32_t> sizes = {3U, 5U, 7U, 11U, 15U};

    for (const uint32_t n : sizes) {
        hbrick::MazeLayout layout(n, n, true);
        // Add deterministic obstacles
        for (uint32_t i = 1U; i < n; i += 3U) {
            layout.setPassable(hbrick::GridCoord{i, i}, false);
            if (i + 1U < n) {
                layout.setPassable(hbrick::GridCoord{i + 1U, i}, false);
            }
        }

        const bool require_sound = (n > 3U);

        // Test BidirectionalAll
        const hbrick::DirectedGridGraph bi_graph = hbrick::DirectedGridGraphBuilder::build(
            layout,
            hbrick::GridEdgeConversionMode::BidirectionalAll
        );
        expectBrickAndHBrickExactAllPairs(
            layout,
            bi_graph,
            hbrick::TileSize{3U, 3U},
            fullDepthConfig(hbrick::TileSize{3U, 3U}, hbrick::GroupSize{2U, 2U}),
            "size-ladder-bi-" + std::to_string(n) + "x" + std::to_string(n),
            require_sound
        );

        // Test RandomAsymmetric
        const hbrick::DirectedGridGraph asym_graph = hbrick::DirectedGridGraphBuilder::build(
            layout,
            hbrick::GridEdgeConversionMode::RandomAsymmetric,
            hbrick::RandomAsymmetricParams{n * 1337ULL, 0.6, 0.4, 0.05, 0.05}
        );
        expectBrickAndHBrickExactAllPairs(
            layout,
            asym_graph,
            hbrick::TileSize{3U, 3U},
            fullDepthConfig(hbrick::TileSize{3U, 3U}, hbrick::GroupSize{2U, 2U}),
            "size-ladder-asym-" + std::to_string(n) + "x" + std::to_string(n),
            require_sound
        );
    }
}
