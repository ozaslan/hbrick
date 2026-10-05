#include <gtest/gtest.h>

#include <limits>
#include <thread>
#include <vector>

#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/baselines/hbrick_query_scratch.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

hbrick::HBrickConfig configFor(
    const hbrick::TileSize base_tile_size,
    const hbrick::GroupSize group_size,
    const uint32_t max_depth
) {
    hbrick::HBrickConfig config{};
    config.base_tile_size = base_tile_size;
    config.group_size = group_size;
    config.max_depth = max_depth;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();
    return config;
}

void expectMatchesBfsOnAllPairs(
    const hbrick::MazeLayout& layout,
    const hbrick::DirectedGridGraph& graph,
    const hbrick::HBrickConfig& config
) {
    hbrick::HBrickBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    const hbrick::CsrGraph& csr = graph.csrGraph();

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            const hbrick::ReachabilityAnswer expected =
                hbrick::Bfs::reachable(csr, source, target, bfs_scratch);
            const hbrick::ReachabilityAnswer actual = baseline.query(source, target);
            EXPECT_EQ(actual, expected) << "source=" << source << " target=" << target;
        }
    }
}

}  // namespace

TEST(HBrickBaseline, MatchesBfsOnEightByEightHierarchy) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{2U, 2U}, false);
    layout.setPassable(hbrick::GridCoord{5U, 5U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{31U, 0.55, 0.45, 0.0, 0.0}
    );

    expectMatchesBfsOnAllPairs(
        layout,
        graph,
        configFor(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 2U)
    );
}

TEST(HBrickBaseline, MatchesBfsOnSixteenBySixteenFullDepth) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    expectMatchesBfsOnAllPairs(
        layout,
        graph,
        configFor(
            hbrick::TileSize{4U, 4U},
            hbrick::GroupSize{2U, 2U},
            hbrick::kHBrickFullDepth
        )
    );
}

TEST(HBrickBaseline, FlatFallbackWhenMaxDepthIsOne) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    expectMatchesBfsOnAllPairs(
        layout,
        graph,
        configFor(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 1U)
    );
}

TEST(HBrickBaseline, SkippedWhenMemoryBudgetExceeded) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::HBrickConfig config =
        configFor(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 2U);
    config.max_memory_bytes = 0U;

    hbrick::HBrickBaseline baseline;
    baseline.preprocess(graph, layout, config);
    EXPECT_EQ(baseline.status(), hbrick::BaselineStatus::SkippedByPolicy);
}

TEST(HBrickBaseline, MatchesBfsOnNineBySevenPartialEdgeTiles) {
    hbrick::MazeLayout layout(9U, 7U);
    layout.setPassable(hbrick::GridCoord{3U, 3U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{17U, 0.55, 0.45, 0.0, 0.0}
    );

    expectMatchesBfsOnAllPairs(
        layout,
        graph,
        configFor(
            hbrick::TileSize{4U, 4U},
            hbrick::GroupSize{2U, 2U},
            hbrick::kHBrickFullDepth
        )
    );
}

TEST(HBrickBaseline, HierarchicalOnlyMatchesBfsOnDeepAsymmetricGrid) {
    // Depth ≥ 2 exercises reverse target exterior propagation at common ancestors
    // above the immediate parent (no flat port-BFS fallback).
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    layout.setPassable(hbrick::GridCoord{11U, 6U}, false);
    layout.setPassable(hbrick::GridCoord{7U, 12U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{41U, 0.6, 0.4, 0.05, 0.05}
    );

    expectMatchesBfsOnAllPairs(
        layout,
        graph,
        configFor(
            hbrick::TileSize{2U, 2U},
            hbrick::GroupSize{2U, 2U},
            hbrick::kHBrickFullDepth
        )
    );
}

TEST(HBrickBaseline, SameTileLocalClosureWhenPortIndexIsEmpty) {
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

    expectMatchesBfsOnAllPairs(
        layout,
        graph,
        configFor(hbrick::TileSize{64U, 64U}, hbrick::GroupSize{2U, 2U}, 1U)
    );
}

TEST(HBrickBaseline, ConcurrentQueriesWithCallerScratch) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    layout.setPassable(hbrick::GridCoord{8U, 8U}, false);
    layout.setPassable(hbrick::GridCoord{12U, 12U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{99U, 0.7, 0.3, 0.05, 0.05}
    );

    hbrick::HBrickBaseline baseline;
    baseline.preprocess(
        graph,
        layout,
        configFor(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 3U)
    );
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    const uint32_t num_vertices = graph.numVertices();
    constexpr uint32_t kNumThreads = 4U;
    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    for (uint32_t thread_id = 0U; thread_id < kNumThreads; ++thread_id) {
        threads.emplace_back([&, thread_id]() {
            hbrick::HBrickQueryScratch scratch{};
            scratch.prepare(baseline.index());
            hbrick::GraphSearchScratch port_bfs_scratch{graph.numVertices()};
            hbrick::GraphSearchScratch bfs_scratch{graph.numVertices()};

            const uint32_t chunk = (num_vertices + kNumThreads - 1U) / kNumThreads;
            const uint32_t s_begin = thread_id * chunk;
            const uint32_t s_end = std::min(s_begin + chunk, num_vertices);

            for (uint32_t s = s_begin; s < s_end; ++s) {
                for (uint32_t t = 0U; t < num_vertices; t += 3U) {
                    const hbrick::ReachabilityAnswer expected =
                        hbrick::Bfs::reachable(graph.csrGraph(), s, t, bfs_scratch);
                    const hbrick::ReachabilityAnswer actual =
                        baseline.query(s, t, scratch, port_bfs_scratch);
                    EXPECT_EQ(actual, expected) << "thread=" << thread_id << " s=" << s << " t=" << t;
                }
            }
        });
    }

    for (std::thread& t : threads) {
        t.join();
    }
}

TEST(HBrickBaseline, MeasuredAndEstimatedStorageBytesAreConsistent) {
    hbrick::MazeLayout layout(16U, 16U);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const hbrick::HBrickConfig config = configFor(
        hbrick::TileSize{4U, 4U},
        hbrick::GroupSize{2U, 2U},
        4U
    );

    hbrick::HBrickBaseline baseline{};
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    const uint64_t estimated = baseline.indexStorageBytes();
    const uint64_t measured = baseline.measuredStorageBytes();

    EXPECT_GT(estimated, 0U);
    EXPECT_GT(measured, 0U);

    // Estimated and measured should be in close agreement now that
    // structural vectors and hierarchy are properly accounted for
    const double ratio = static_cast<double>(measured) / static_cast<double>(estimated);
    EXPECT_GE(ratio, 0.95);
    EXPECT_LE(ratio, 1.05);
}

