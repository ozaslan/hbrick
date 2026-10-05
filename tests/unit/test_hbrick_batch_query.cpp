/**
 * @file test_hbrick_batch_query.cpp
 * @brief Correctness of the many-source/many-target H-BRICK batch query against
 *        the scalar skip-lift query and the BFS oracle.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/baselines/hbrick_batch_query.hpp"
#include "hbrick/baselines/hbrick_skip_lift_baseline.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/group_size.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"
#include "maze_generator.hpp"

namespace {

using namespace hbrick;

HBrickConfig configFor(const TileSize base, const GroupSize group) {
    HBrickConfig config{};
    config.base_tile_size = base;
    config.group_size = group;
    config.max_depth = kHBrickFullDepth;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();
    return config;
}

/** @brief SplitMix64 for deterministic endpoint sampling inside the test. */
class Rng {
public:
    explicit Rng(const uint64_t seed) : state_(seed) {}

    uint64_t next() {
        state_ += 0x9E3779B97F4A7C15ULL;
        uint64_t z = state_;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    uint32_t uniform(const uint32_t bound) {
        return static_cast<uint32_t>(next() % bound);
    }

private:
    uint64_t state_ = 0U;
};

std::vector<uint32_t> sampleDistinctPassable(
    Rng& rng,
    const DirectedGridGraph& graph,
    const uint32_t count
) {
    std::vector<uint32_t> vertices;
    vertices.reserve(count);
    while (vertices.size() < count) {
        const uint32_t candidate = rng.uniform(graph.numVertices());
        if (std::find(vertices.begin(), vertices.end(), candidate) == vertices.end()) {
            vertices.push_back(candidate);
        }
    }
    return vertices;
}

void expectBatchMatchesScalarAndBfs(
    const MazeLayout& layout,
    const DirectedGridGraph& graph,
    const HBrickConfig& config,
    const std::vector<uint32_t>& sources,
    const std::vector<uint32_t>& targets,
    const std::string& context
) {
    HBrickSkipLiftBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), BaselineStatus::Completed) << context;

    const uint32_t num_sources = static_cast<uint32_t>(sources.size());
    const uint32_t num_targets = static_cast<uint32_t>(targets.size());
    ASSERT_TRUE(baseline.prepareBatch(num_sources, num_targets)) << context;
    EXPECT_GT(baseline.batchScratchMemoryBytes(), 0U) << context;

    std::vector<uint8_t> batch(static_cast<size_t>(num_sources) * num_targets, 0U);
    HBrickBatchQueryStats stats{};
    baseline.batchQuery(sources, targets, batch, &stats);
    EXPECT_EQ(stats.pairs, num_sources * num_targets) << context;

    GraphSearchScratch bfs_scratch(graph.numVertices());
    const CsrGraph& csr = graph.csrGraph();

    for (uint32_t i = 0U; i < num_sources; ++i) {
        for (uint32_t j = 0U; j < num_targets; ++j) {
            const size_t pair = static_cast<size_t>(i) * num_targets + j;

            const HBrickQueryOutcome scalar =
                baseline.queryDetailed(sources[i], targets[j]);
            const uint8_t scalar_flag =
                scalar.answer == ReachabilityAnswer::Reachable ? 1U : 0U;
            EXPECT_EQ(batch[pair], scalar_flag)
                << context << " scalar mismatch i=" << i << " j=" << j
                << " source=" << sources[i] << " target=" << targets[j];

            if (sources[i] >= graph.numVertices() || targets[j] >= graph.numVertices()) {
                EXPECT_EQ(batch[pair], 0U)
                    << context << " out-of-range pair must be unreachable";
                continue;
            }

            const uint8_t oracle =
                (Bfs::reachable(csr, sources[i], targets[j], bfs_scratch)
                 == ReachabilityAnswer::Reachable)
                ? 1U
                : 0U;
            EXPECT_EQ(batch[pair], oracle)
                << context << " bfs mismatch i=" << i << " j=" << j
                << " source=" << sources[i] << " target=" << targets[j];
        }
    }
}

}  // namespace

TEST(HBrickBatchQuery, MatchesScalarAndBfsOnPerfectMaze) {
    const MazeLayout layout =
        hbrick::test_support::generatePerfectMaze({12U, 12U, 0xA5A5U});
    RandomAsymmetricParams params{};
    params.seed = 4242U;
    const DirectedGridGraph graph =
        DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::RandomAsymmetric, params);

    const HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});
    Rng rng(0xBEEFULL);

    {
        const std::vector<uint32_t> sources = sampleDistinctPassable(rng, graph, 24U);
        const std::vector<uint32_t> targets = sampleDistinctPassable(rng, graph, 24U);
        expectBatchMatchesScalarAndBfs(
            layout,
            graph,
            config,
            sources,
            targets,
            "perfect maze 24x24"
        );
    }

    {
        const std::vector<uint32_t> sources = sampleDistinctPassable(rng, graph, 5U);
        const std::vector<uint32_t> targets = sampleDistinctPassable(rng, graph, 13U);
        expectBatchMatchesScalarAndBfs(layout, graph, config, sources, targets, "perfect maze 5x13");
    }

    {
        // Same-tile batch: all endpoints in the top-left base tile [0,4) x [0,4).
        std::vector<uint32_t> tile_vertices;
        for (uint32_t vertex = 0U;
             vertex < graph.numVertices() && tile_vertices.size() < 10U;
             ++vertex) {
            const GridCoord coord = graph.coordFromVertex(vertex);
            if (coord.x < 4U && coord.y < 4U) {
                tile_vertices.push_back(vertex);
            }
        }
        ASSERT_GE(tile_vertices.size(), 4U);
        const auto middle = tile_vertices.begin() + static_cast<long>(tile_vertices.size() / 2U);
        const std::vector<uint32_t> sources(tile_vertices.begin(), middle);
        const std::vector<uint32_t> targets(middle, tile_vertices.end());
        expectBatchMatchesScalarAndBfs(
            layout,
            graph,
            config,
            sources,
            targets,
            "perfect maze same-tile"
        );
    }
}

TEST(HBrickBatchQuery, MatchesScalarAndBfsOnOpenBidirectionalGrid) {
    const MazeLayout layout(24U, 24U, true);
    const DirectedGridGraph graph =
        DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);

    const HBrickConfig config = configFor(TileSize{8U, 8U}, GroupSize{2U, 2U});
    Rng rng(0xC0FFEEULL);
    const std::vector<uint32_t> sources = sampleDistinctPassable(rng, graph, 9U);
    const std::vector<uint32_t> targets = sampleDistinctPassable(rng, graph, 9U);
    expectBatchMatchesScalarAndBfs(
        layout,
        graph,
        config,
        sources,
        targets,
        "open bidirectional 9x9"
    );
}

TEST(HBrickBatchQuery, OutOfRangeEndpointsAreUnreachable) {
    const MazeLayout layout(12U, 12U, true);
    const DirectedGridGraph graph =
        DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);
    const HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});

    const std::vector<uint32_t> sources{1U, 2000000000U};
    const std::vector<uint32_t> targets{2U, 3U, 2000000001U};
    expectBatchMatchesScalarAndBfs(
        layout,
        graph,
        config,
        sources,
        targets,
        "out-of-range endpoints"
    );
}

TEST(HBrickBatchQuery, ExhaustiveAllPairsMatchBfsOnSmallMaze) {
    const MazeLayout layout =
        hbrick::test_support::generatePerfectMaze({8U, 8U, 0x1234U});
    RandomAsymmetricParams params{};
    params.seed = 99U;
    const DirectedGridGraph graph =
        DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::RandomAsymmetric, params);

    const HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});
    HBrickSkipLiftBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), BaselineStatus::Completed);

    const uint32_t num_vertices = graph.numVertices();
    std::vector<uint32_t> all(num_vertices);
    for (uint32_t vertex = 0U; vertex < num_vertices; ++vertex) {
        all[vertex] = vertex;
    }
    ASSERT_TRUE(baseline.prepareBatch(num_vertices, num_vertices));
    std::vector<uint8_t> batch(static_cast<size_t>(num_vertices) * num_vertices, 0U);
    baseline.batchQuery(all, all, batch, nullptr);

    GraphSearchScratch bfs_scratch(num_vertices);
    const CsrGraph& csr = graph.csrGraph();
    std::vector<uint8_t> row_oracle(num_vertices, 0U);
    for (uint32_t source = 0U; source < num_vertices; ++source) {
        for (uint32_t target = 0U; target < num_vertices; ++target) {
            row_oracle[target] =
                (Bfs::reachable(csr, source, target, bfs_scratch)
                 == ReachabilityAnswer::Reachable)
                ? 1U
                : 0U;
        }
        for (uint32_t target = 0U; target < num_vertices; ++target) {
            ASSERT_EQ(batch[static_cast<size_t>(source) * num_vertices + target],
                      row_oracle[target])
                << "source=" << source << " target=" << target;
        }
    }
}

TEST(HBrickBatchQuery, FallbackPathMatchesScalarWhenHierarchyNotSound) {
    const MazeLayout layout =
        hbrick::test_support::generatePerfectMaze({10U, 10U, 0x77U});
    RandomAsymmetricParams params{};
    params.seed = 5150U;
    const DirectedGridGraph graph =
        DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::RandomAsymmetric, params);

    HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});
    config.max_depth = 1U;  // Stop above the base level so many roots remain.

    Rng rng(0xF00DULL);
    const std::vector<uint32_t> sources = sampleDistinctPassable(rng, graph, 11U);
    const std::vector<uint32_t> targets = sampleDistinctPassable(rng, graph, 11U);
    expectBatchMatchesScalarAndBfs(
        layout,
        graph,
        config,
        sources,
        targets,
        "max_depth=1 fallback"
    );
}

TEST(HBrickBatchQuery, ReusesPreparedWorkspaceAcrossSizes) {
    const MazeLayout layout(16U, 16U, true);
    const DirectedGridGraph graph =
        DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);
    const HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});

    HBrickSkipLiftBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), BaselineStatus::Completed);
    ASSERT_TRUE(baseline.prepareBatch(12U, 12U));

    const std::vector<uint32_t> small_sources{0U, 5U, 17U};
    const std::vector<uint32_t> small_targets{7U, 31U};
    std::vector<uint8_t> small_out(6U, 0U);
    baseline.batchQuery(small_sources, small_targets, small_out, nullptr);
    for (const uint8_t flag : small_out) {
        EXPECT_EQ(flag, 1U);
    }

    Rng rng(7U);
    const std::vector<uint32_t> big_sources = sampleDistinctPassable(rng, graph, 12U);
    const std::vector<uint32_t> big_targets = sampleDistinctPassable(rng, graph, 12U);
    std::vector<uint8_t> big_out(144U, 0U);
    baseline.batchQuery(big_sources, big_targets, big_out, nullptr);
    for (const uint8_t flag : big_out) {
        EXPECT_EQ(flag, 1U);
    }
}

TEST(HBrickBatchQuery, ConfinedSourceSameTilePairStaysReachable) {
    // Regression for the ordering between the same-tile local test and the
    // confined-endpoint verdict. On this fully one-way grid the source 10 sits
    // in a tile whose reachable ports are all pruned at the map border, so its
    // boundary frontier is empty, yet the target 11 is reachable inside the
    // tile. The batch must answer reachable exactly as the scalar query does.
    const MazeLayout layout(12U, 12U, true);
    RandomAsymmetricParams params{};
    params.seed = 1U;
    params.p_bidirectional = 0.0L;
    params.p_one_way = 1.0L;
    const DirectedGridGraph graph = DirectedGridGraphBuilder::build(
        layout,
        GridEdgeConversionMode::RandomAsymmetric,
        params
    );

    const HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});
    GraphSearchScratch oracle_scratch(graph.numVertices());
    EXPECT_EQ(
        Bfs::reachable(graph.csrGraph(), 10U, 11U, oracle_scratch),
        ReachabilityAnswer::Reachable
    );

    const uint32_t num_vertices = graph.numVertices();
    std::vector<uint32_t> all(num_vertices);
    std::iota(all.begin(), all.end(), 0U);
    expectBatchMatchesScalarAndBfs(
        layout,
        graph,
        config,
        all,
        all,
        "confined same-tile regression"
    );

    HBrickSkipLiftBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), BaselineStatus::Completed);
    EXPECT_EQ(baseline.query(10U, 11U), ReachabilityAnswer::Reachable);
}

TEST(HBrickBatchQuery, StaleWorkspaceAcrossIndexReloadDoesNotCrash) {
    // Regression for stale internal batch scratch across re-preprocessing.
    const MazeLayout layout_small(4U, 4U, true);
    RandomAsymmetricParams params{};
    params.seed = 42U;
    const DirectedGridGraph graph_small = DirectedGridGraphBuilder::build(
        layout_small,
        GridEdgeConversionMode::RandomAsymmetric,
        params
    );
    const HBrickConfig config_small = configFor(TileSize{2U, 2U}, GroupSize{2U, 2U});

    HBrickSkipLiftBaseline baseline;
    baseline.preprocess(graph_small, layout_small, config_small);
    ASSERT_EQ(baseline.status(), BaselineStatus::Completed);
    ASSERT_TRUE(baseline.prepareBatch(1U, 1U));

    // Re-preprocess the same baseline instance on a larger graph with deeper hierarchy.
    const MazeLayout layout_large(32U, 32U, true);
    const DirectedGridGraph graph_large = DirectedGridGraphBuilder::build(
        layout_large,
        GridEdgeConversionMode::RandomAsymmetric,
        params
    );
    const HBrickConfig config_large = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});
    baseline.preprocess(graph_large, layout_large, config_large);
    ASSERT_EQ(baseline.status(), BaselineStatus::Completed);

    // Calling batchQuery without prepareBatch on the new index must safely return false
    // (internal batch scratch was reset upon re-preprocess).
    const std::vector<uint32_t> sources{0U};
    const std::vector<uint32_t> targets{1U};
    std::vector<uint8_t> out(1U, 99U);
    EXPECT_FALSE(baseline.batchQuery(sources, targets, out, nullptr));
    // Output must remain untouched upon rejection.
    EXPECT_EQ(out[0], 99U);

    // After re-preparing batch scratch for the new index, it functions normally
    // and matches both scalar query and BFS oracle.
    ASSERT_TRUE(baseline.prepareBatch(1U, 1U));
    EXPECT_TRUE(baseline.batchQuery(sources, targets, out, nullptr));
    const uint8_t scalar_expected =
        baseline.query(sources[0], targets[0]) == ReachabilityAnswer::Reachable ? 1U : 0U;
    EXPECT_EQ(out[0], scalar_expected);

    GraphSearchScratch bfs_scratch(graph_large.numVertices());
    const uint8_t bfs_expected =
        (Bfs::reachable(graph_large.csrGraph(), sources[0], targets[0], bfs_scratch)
         == ReachabilityAnswer::Reachable) ? 1U : 0U;
    EXPECT_EQ(out[0], bfs_expected);
}

TEST(HBrickBatchQuery, StaleCallerOwnedScratchIsRejected) {
    // Caller-owned scratch prepared for a different index must be safely rejected.
    const MazeLayout layout_a(8U, 8U, true);
    RandomAsymmetricParams params{};
    params.seed = 100U;
    const DirectedGridGraph graph_a = DirectedGridGraphBuilder::build(
        layout_a,
        GridEdgeConversionMode::RandomAsymmetric,
        params
    );
    const HBrickConfig config_a = configFor(TileSize{2U, 2U}, GroupSize{2U, 2U});

    HBrickIndex index_a = HBrickIndex::build(graph_a, layout_a, config_a);
    HBrickBatchScratch scratch_a;
    scratch_a.prepare(index_a, 2U, 2U);

    const MazeLayout layout_b(32U, 32U, true);
    const DirectedGridGraph graph_b = DirectedGridGraphBuilder::build(
        layout_b,
        GridEdgeConversionMode::RandomAsymmetric,
        params
    );
    const HBrickConfig config_b = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});

    HBrickSkipLiftBaseline baseline_b;
    baseline_b.preprocess(graph_b, layout_b, config_b);
    ASSERT_EQ(baseline_b.status(), BaselineStatus::Completed);

    const std::vector<uint32_t> sources{0U, 1U};
    const std::vector<uint32_t> targets{2U, 3U};
    std::vector<uint8_t> out(4U, 99U);
    HBrickBatchQueryStats stats{};
    EXPECT_FALSE(baseline_b.batchQuery(scratch_a, sources, targets, out, &stats));
    // Scratch A is incompatible with index B, query must reject without heap corruption or modifying output.
    for (const uint8_t flag : out) {
        EXPECT_EQ(flag, 99U);
    }
}

TEST(HBrickBatchQuery, ConcurrentCallerOwnedScratchIsRaceFree) {
    // Verifies that multiple threads running batch queries with independent
    // caller-owned scratches against the same shared baseline run race-free.
    const MazeLayout layout(16U, 16U, true);
    RandomAsymmetricParams params{};
    params.seed = 303U;
    const DirectedGridGraph graph = DirectedGridGraphBuilder::build(
        layout,
        GridEdgeConversionMode::RandomAsymmetric,
        params
    );
    const HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});

    HBrickSkipLiftBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), BaselineStatus::Completed);

    constexpr uint32_t kNumThreads = 4U;
    constexpr uint32_t kSourcesPerThread = 8U;
    constexpr uint32_t kTargetsPerThread = 8U;

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    std::atomic<bool> all_passed{true};

    for (uint32_t t = 0U; t < kNumThreads; ++t) {
        threads.emplace_back([&, t]() {
            HBrickBatchScratch scratch;
            scratch.prepare(baseline.index(), kSourcesPerThread, kTargetsPerThread);

            Rng rng(1000ULL + t * 37ULL);
            const std::vector<uint32_t> thread_sources =
                sampleDistinctPassable(rng, graph, kSourcesPerThread);
            const std::vector<uint32_t> thread_targets =
                sampleDistinctPassable(rng, graph, kTargetsPerThread);
            std::vector<uint8_t> thread_out(kSourcesPerThread * kTargetsPerThread, 0U);

            for (uint32_t iter = 0U; iter < 10U; ++iter) {
                if (!baseline.batchQuery(scratch, thread_sources, thread_targets, thread_out)) {
                    all_passed.store(false);
                    return;
                }
                for (uint32_t i = 0U; i < kSourcesPerThread; ++i) {
                    for (uint32_t j = 0U; j < kTargetsPerThread; ++j) {
                        const size_t pair = static_cast<size_t>(i) * kTargetsPerThread + j;
                        const uint8_t expected =
                            baseline.query(thread_sources[i], thread_targets[j])
                            == ReachabilityAnswer::Reachable ? 1U : 0U;
                        if (thread_out[pair] != expected) {
                            all_passed.store(false);
                            return;
                        }
                    }
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }
    EXPECT_TRUE(all_passed.load());
}

TEST(HBrickBatchQuery, EmptyAndZeroSizeBatchesReturnFalseSafely) {
    const MazeLayout layout(8U, 8U, true);
    const DirectedGridGraph graph =
        DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);
    const HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});

    HBrickSkipLiftBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), BaselineStatus::Completed);
    ASSERT_TRUE(baseline.prepareBatch(4U, 4U));

    const std::vector<uint32_t> empty_list{};
    const std::vector<uint32_t> valid_list{0U, 1U};
    std::vector<uint8_t> out{77U, 88U};

    // 0 sources, 2 targets
    EXPECT_FALSE(baseline.batchQuery(empty_list, valid_list, out));
    EXPECT_EQ(out[0], 77U);
    EXPECT_EQ(out[1], 88U);

    // 2 sources, 0 targets
    EXPECT_FALSE(baseline.batchQuery(valid_list, empty_list, out));
    EXPECT_EQ(out[0], 77U);
    EXPECT_EQ(out[1], 88U);

    // 0 sources, 0 targets
    EXPECT_FALSE(baseline.batchQuery(empty_list, empty_list, out));

    // Calling before preprocessing on a blank baseline
    HBrickSkipLiftBaseline blank_baseline;
    EXPECT_FALSE(blank_baseline.batchQuery(valid_list, valid_list, out));
}

TEST(HBrickBatchQuery, AsymmetricBroadcastAndGatherAcrossFourLevelHierarchy) {
    // 32x32 grid with base tile 4x4 and group size 2x2 produces 4 hierarchy levels:
    // Level 0 (base: 8x8 tiles), Level 1 (4x4), Level 2 (2x2), Level 3 (root: 1x1).
    const MazeLayout layout =
        hbrick::test_support::generatePerfectMaze({32U, 32U, 0xACE1U});
    RandomAsymmetricParams params{};
    params.seed = 777U;
    const DirectedGridGraph graph =
        DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::RandomAsymmetric, params);
    const HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});

    Rng rng(0x12345678ULL);

    // 1-to-64 Broadcast
    {
        const std::vector<uint32_t> sources = sampleDistinctPassable(rng, graph, 1U);
        const std::vector<uint32_t> targets = sampleDistinctPassable(rng, graph, 64U);
        expectBatchMatchesScalarAndBfs(
            layout,
            graph,
            config,
            sources,
            targets,
            "broadcast 1x64 on 4-level hierarchy"
        );
    }

    // 64-to-1 Gather
    {
        const std::vector<uint32_t> sources = sampleDistinctPassable(rng, graph, 64U);
        const std::vector<uint32_t> targets = sampleDistinctPassable(rng, graph, 1U);
        expectBatchMatchesScalarAndBfs(
            layout,
            graph,
            config,
            sources,
            targets,
            "gather 64x1 on 4-level hierarchy"
        );
    }
}

TEST(HBrickBatchQuery, StatsAccountingAndConservation) {
    const MazeLayout layout(32U, 32U, true);
    const DirectedGridGraph graph =
        DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::BidirectionalAll);
    const HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});

    HBrickSkipLiftBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), BaselineStatus::Completed);

    constexpr uint32_t kSources = 16U;
    constexpr uint32_t kTargets = 16U;
    constexpr uint32_t kPairs = kSources * kTargets;
    ASSERT_TRUE(baseline.prepareBatch(kSources, kTargets));

    Rng rng(0xFEEDFACEULL);
    const std::vector<uint32_t> sources = sampleDistinctPassable(rng, graph, kSources);
    const std::vector<uint32_t> targets = sampleDistinctPassable(rng, graph, kTargets);
    std::vector<uint8_t> out(kPairs, 0U);

    HBrickBatchQueryStats stats{};
    ASSERT_TRUE(baseline.batchQuery(sources, targets, out, &stats));

    EXPECT_EQ(stats.pairs, kPairs);
    // Every pair must be uniquely accounted for across outcomes.
    EXPECT_EQ(
        stats.local_hits + stats.ancestor_hits + stats.fallbacks + stats.hierarchy_negatives,
        stats.pairs
    );
    EXPECT_LE(stats.pair_tests, stats.pairs);
    EXPECT_GT(stats.source_lifts, 0U);
    EXPECT_GT(stats.target_lifts, 0U);
    EXPECT_LE(stats.max_level_reached, baseline.index().hierarchy().numLevels());
}

TEST(HBrickBatchQuery, DisconnectedRoomsBatchMatchesOracle) {
    // 16x16 grid with a cross-shaped impenetrable wall at x=8 and y=8,
    // forming 4 disconnected quadrants.
    MazeLayout layout(16U, 16U, true);
    for (uint32_t y = 0U; y < 16U; ++y) {
        layout.setPassable(8U, y, false);
    }
    for (uint32_t x = 0U; x < 16U; ++x) {
        layout.setPassable(x, 8U, false);
    }

    RandomAsymmetricParams params{};
    params.seed = 555U;
    const DirectedGridGraph graph =
        DirectedGridGraphBuilder::build(layout, GridEdgeConversionMode::RandomAsymmetric, params);
    const HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});

    std::vector<uint32_t> passable_vertices;
    for (uint32_t v = 0U; v < graph.numVertices(); ++v) {
        const GridCoord c = graph.coordFromVertex(v);
        if (c.x != 8U && c.y != 8U) {
            passable_vertices.push_back(v);
        }
    }
    ASSERT_GE(passable_vertices.size(), 32U);

    Rng rng(0xCAFEULL);
    std::vector<uint32_t> sources;
    std::vector<uint32_t> targets;
    while (sources.size() < 16U) {
        const uint32_t idx = rng.uniform(static_cast<uint32_t>(passable_vertices.size()));
        const uint32_t v = passable_vertices[idx];
        if (std::find(sources.begin(), sources.end(), v) == sources.end()) {
            sources.push_back(v);
        }
    }
    while (targets.size() < 16U) {
        const uint32_t idx = rng.uniform(static_cast<uint32_t>(passable_vertices.size()));
        const uint32_t v = passable_vertices[idx];
        if (std::find(targets.begin(), targets.end(), v) == targets.end()) {
            targets.push_back(v);
        }
    }

    expectBatchMatchesScalarAndBfs(
        layout,
        graph,
        config,
        sources,
        targets,
        "disconnected 4-room grid"
    );
}

TEST(HBrickBatchQuery, StressHighConcurrencyEightThreadsCallerOwnedScratch) {
    // 8 concurrent threads executing batch queries with different dimensions
    // simultaneously against a shared 32x32 multi-level baseline.
    const MazeLayout layout(32U, 32U, true);
    RandomAsymmetricParams params{};
    params.seed = 919U;
    const DirectedGridGraph graph = DirectedGridGraphBuilder::build(
        layout,
        GridEdgeConversionMode::RandomAsymmetric,
        params
    );
    const HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});

    HBrickSkipLiftBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), BaselineStatus::Completed);

    constexpr uint32_t kNumThreads = 8U;
    constexpr uint32_t kItersPerThread = 25U;

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    std::atomic<bool> all_passed{true};

    for (uint32_t t = 0U; t < kNumThreads; ++t) {
        threads.emplace_back([&, t]() {
            const uint32_t num_s = 4U + (t % 4U) * 4U;   // 4, 8, 12, 16
            const uint32_t num_t = 16U - (t % 4U) * 2U;  // 16, 14, 12, 10
            HBrickBatchScratch scratch;
            scratch.prepare(baseline.index(), num_s, num_t);

            Rng rng(5000ULL + t * 101ULL);
            std::vector<uint8_t> out(static_cast<size_t>(num_s) * num_t, 0U);

            for (uint32_t iter = 0U; iter < kItersPerThread; ++iter) {
                const std::vector<uint32_t> sources = sampleDistinctPassable(rng, graph, num_s);
                const std::vector<uint32_t> targets = sampleDistinctPassable(rng, graph, num_t);

                if (!baseline.batchQuery(scratch, sources, targets, out)) {
                    all_passed.store(false);
                    return;
                }

                for (uint32_t i = 0U; i < num_s; ++i) {
                    for (uint32_t j = 0U; j < num_t; ++j) {
                        const size_t pair = static_cast<size_t>(i) * num_t + j;
                        const uint8_t expected =
                            baseline.query(sources[i], targets[j])
                            == ReachabilityAnswer::Reachable ? 1U : 0U;
                        if (out[pair] != expected) {
                            all_passed.store(false);
                            return;
                        }
                    }
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }
    EXPECT_TRUE(all_passed.load());
}

TEST(HBrickBatchQuery, MassiveFiftyThousandQueriesMatchScalarAndOracle) {
    const MazeLayout layout(32U, 32U, true);
    RandomAsymmetricParams params{};
    params.seed = 2026U;
    const DirectedGridGraph graph = DirectedGridGraphBuilder::build(
        layout,
        GridEdgeConversionMode::RandomAsymmetric,
        params
    );
    const HBrickConfig config = configFor(TileSize{4U, 4U}, GroupSize{2U, 2U});

    HBrickSkipLiftBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), BaselineStatus::Completed);

    constexpr uint32_t kK = 32U;
    constexpr uint32_t kPairs = kK * kK;  // 1,024 pairs per batch
    constexpr uint32_t kBatches = 50U;    // 50 batches = 51,200 pairs
    ASSERT_TRUE(baseline.prepareBatch(kK, kK));

    Rng rng(0xCAFEBABEDEADULL);
    std::vector<uint8_t> batch_out(kPairs, 0U);
    GraphSearchScratch bfs_scratch(graph.numVertices());
    const CsrGraph& csr = graph.csrGraph();

    for (uint32_t b = 0U; b < kBatches; ++b) {
        const std::vector<uint32_t> sources = sampleDistinctPassable(rng, graph, kK);
        const std::vector<uint32_t> targets = sampleDistinctPassable(rng, graph, kK);

        ASSERT_TRUE(baseline.batchQuery(sources, targets, batch_out, nullptr));

        for (uint32_t i = 0U; i < kK; ++i) {
            for (uint32_t j = 0U; j < kK; ++j) {
                const size_t pair = static_cast<size_t>(i) * kK + j;
                const uint8_t scalar =
                    baseline.query(sources[i], targets[j]) == ReachabilityAnswer::Reachable ? 1U : 0U;
                ASSERT_EQ(batch_out[pair], scalar)
                    << "batch " << b << " i=" << i << " j=" << j;

                if ((i + j + b) % 19U == 0U) {
                    const uint8_t oracle =
                        Bfs::reachable(csr, sources[i], targets[j], bfs_scratch) == ReachabilityAnswer::Reachable
                        ? 1U
                        : 0U;
                    ASSERT_EQ(batch_out[pair], oracle);
                }
            }
        }
    }
}


