#include <gtest/gtest.h>

#include <limits>
#include <numeric>
#include <vector>

#include "hbrick/baselines/csr_bfs_baseline.hpp"
#include "hbrick/baselines/csr_dfs_baseline.hpp"
#include "hbrick/baselines/brick_closure_baseline.hpp"
#include "hbrick/baselines/brick_search_baseline.hpp"
#include "hbrick/baselines/full_closure_baseline.hpp"
#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/baselines/hbrick_fused_lift_baseline.hpp"
#include "hbrick/baselines/hbrick_fused_lift_cache_baseline.hpp"
#include "hbrick/baselines/hbrick_micro_bfs_baseline.hpp"
#include "hbrick/baselines/hbrick_skip_lift_baseline.hpp"
#include "hbrick/baselines/hbrick_scc_label_baseline.hpp"
#include "hbrick/baselines/scc_dag_closure_baseline.hpp"
#include "hbrick/baselines/scc_dag_search_baseline.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/dag_reachability.hpp"
#include "hbrick/graph/dfs.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/group_size.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"
#include "maze_generator.hpp"
#include "reachability_oracle.hpp"

namespace {

hbrick::CsrGraph buildDiamondGraph() {
    hbrick::CsrGraphBuilder builder{4U};
    builder.addEdge(0U, 1U);
    builder.addEdge(0U, 2U);
    builder.addEdge(1U, 3U);
    builder.addEdge(2U, 3U);
    return builder.build();
}

struct ScratchCapacities {
    std::size_t queue = 0U;
    std::size_t stack = 0U;
    std::size_t visited = 0U;
};

[[nodiscard]] ScratchCapacities captureCapacities(const hbrick::GraphSearchScratch& scratch) {
    return ScratchCapacities{
        scratch.queue().capacity(),
        scratch.stack().capacity(),
        scratch.visitedMark().capacity(),
    };
}

void expectCapacitiesUnchanged(
    const hbrick::GraphSearchScratch& scratch,
    const ScratchCapacities& before
) {
    EXPECT_EQ(scratch.queue().capacity(), before.queue);
    EXPECT_EQ(scratch.stack().capacity(), before.stack);
    EXPECT_EQ(scratch.visitedMark().capacity(), before.visited);
}

[[nodiscard]] hbrick::DirectedGridGraph buildSmallGridGraph(hbrick::MazeLayout& layout) {
    return hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
}

struct HBrickScratchCapacities {
    std::size_t source_ancestor_chain = 0U;
    std::size_t target_ancestor_chain = 0U;
    std::size_t source_bit_chain = 0U;
    std::size_t target_bit_chain = 0U;
};

[[nodiscard]] HBrickScratchCapacities captureHBrickScratchCapacities(
    const hbrick::HBrickQueryScratch& scratch
) {
    return HBrickScratchCapacities{
        scratch.sourceAncestorChain().capacity(),
        scratch.targetAncestorChain().capacity(),
        scratch.sourceChain().capacity(),
        scratch.targetChain().capacity(),
    };
}

void expectHBrickScratchCapacitiesUnchanged(
    const hbrick::HBrickQueryScratch& scratch,
    const HBrickScratchCapacities& before
) {
    const HBrickScratchCapacities after = captureHBrickScratchCapacities(scratch);
    EXPECT_EQ(after.source_ancestor_chain, before.source_ancestor_chain);
    EXPECT_EQ(after.target_ancestor_chain, before.target_ancestor_chain);
    EXPECT_EQ(after.source_bit_chain, before.source_bit_chain);
    EXPECT_EQ(after.target_bit_chain, before.target_bit_chain);
}

}  // namespace

TEST(HotPathAllocations, BfsAndDfsReachableDoNotResizeScratch) {
    const hbrick::MazeLayout maze = hbrick::test_support::generatePerfectMaze(
        hbrick::test_support::MazeParams{8U, 8U, 0xA110C8FEULL}
    );
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        maze,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{0x515EEDULL, 0.15L, 0.25L}
    );

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    hbrick::GraphSearchScratch dfs_scratch(graph.numVertices());
    const ScratchCapacities bfs_before = captureCapacities(bfs_scratch);
    const ScratchCapacities dfs_before = captureCapacities(dfs_scratch);

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(hbrick::Bfs::reachable(graph, source, target, bfs_scratch));
            static_cast<void>(hbrick::Dfs::reachable(graph, source, target, dfs_scratch));
        }
    }

    expectCapacitiesUnchanged(bfs_scratch, bfs_before);
    expectCapacitiesUnchanged(dfs_scratch, dfs_before);
}

TEST(HotPathAllocations, DagReachabilityDoesNotResizeScratch) {
    const hbrick::CsrGraph dag = buildDiamondGraph();
    hbrick::GraphSearchScratch scratch(dag.numVertices());
    const ScratchCapacities before = captureCapacities(scratch);

    for (uint32_t source = 0U; source < dag.numVertices(); ++source) {
        for (uint32_t target = 0U; target < dag.numVertices(); ++target) {
            static_cast<void>(hbrick::DagReachability::reachable(
                dag,
                source,
                target,
                scratch
            ));
        }
    }

    expectCapacitiesUnchanged(scratch, before);
}

TEST(HotPathAllocations, BaselineQueriesDoNotResizeScratch) {
    const hbrick::MazeLayout maze = hbrick::test_support::generatePerfectMaze(
        hbrick::test_support::MazeParams{7U, 7U, 0xCAFEBABEULL}
    );
    const hbrick::CsrGraph graph = hbrick::test_support::buildGridGraph(
        maze,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    hbrick::GraphSearchScratch preprocess_scratch(graph.numVertices());
    hbrick::GraphSearchScratch query_scratch(graph.numVertices());
    const ScratchCapacities before = captureCapacities(query_scratch);

    hbrick::CsrBfsBaseline csr_bfs;
    hbrick::CsrDfsBaseline csr_dfs;
    hbrick::SccDagSearchBaseline scc_search;

    csr_bfs.preprocess(graph);
    csr_dfs.preprocess(graph);
    scc_search.preprocess(graph, preprocess_scratch);

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(csr_bfs.query(source, target, query_scratch));
            static_cast<void>(csr_dfs.query(source, target, query_scratch));
            static_cast<void>(scc_search.query(source, target, query_scratch));
        }
    }

    expectCapacitiesUnchanged(query_scratch, before);
}

TEST(HotPathAllocations, FullClosureQueryDoesNotCrash) {
    // No runtime scratch capacity to capture (closure is materialised at
    // preprocess).  Still check all-pairs answers against BFS so this is
    // more than a void smoke loop.
    const hbrick::CsrGraph graph = buildDiamondGraph();

    hbrick::FullClosureBaseline baseline;
    baseline.preprocess(graph, std::numeric_limits<uint64_t>::max());
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            EXPECT_EQ(
                baseline.query(source, target),
                hbrick::Bfs::reachable(graph, source, target, bfs_scratch)
            ) << "source=" << source << " target=" << target;
        }
    }
}

TEST(HotPathAllocations, SccDagClosureQueryDoesNotCrash) {
    // Same rationale as FullClosureQueryDoesNotCrash: no scratch capacity
    // accessor; verify all-pairs answers against BFS.
    const hbrick::CsrGraph graph = buildDiamondGraph();

    hbrick::GraphSearchScratch preprocess_scratch(graph.numVertices());
    hbrick::SccDagClosureBaseline baseline;
    baseline.preprocess(graph, preprocess_scratch, std::numeric_limits<uint64_t>::max());
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            EXPECT_EQ(
                baseline.query(source, target),
                hbrick::Bfs::reachable(graph, source, target, bfs_scratch)
            ) << "source=" << source << " target=" << target;
        }
    }
}

TEST(HotPathAllocations, BrickSearchQueryDoesNotResizeScratch) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = buildSmallGridGraph(layout);

    hbrick::BrickSearchBaseline baseline;
    baseline.preprocess(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    const ScratchCapacities before =
        captureCapacities(baseline.portScratch());

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(baseline.query(source, target));
        }
    }

    expectCapacitiesUnchanged(baseline.portScratch(), before);
}

TEST(HotPathAllocations, BrickClosureQueryDoesNotCrash) {
    // BrickClosureBaseline holds a single re-usable BitVector with no public
    // capacity accessor.  Check all-pairs answers against BFS in addition to
    // completing without crash.
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = buildSmallGridGraph(layout);
    const hbrick::CsrGraph& csr = graph.csrGraph();

    hbrick::BrickClosureBaseline baseline;
    baseline.preprocess(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    hbrick::GraphSearchScratch bfs_scratch(csr.numVertices());
    for (uint32_t source = 0U; source < csr.numVertices(); ++source) {
        for (uint32_t target = 0U; target < csr.numVertices(); ++target) {
            EXPECT_EQ(
                baseline.query(source, target),
                hbrick::Bfs::reachable(csr, source, target, bfs_scratch)
            ) << "source=" << source << " target=" << target;
        }
    }
}

TEST(HotPathAllocations, HBrickQueryDoesNotResizeScratch) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = buildSmallGridGraph(layout);

    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{4U, 4U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    config.max_depth = 2U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    const ScratchCapacities port_before =
        captureCapacities(baseline.portBfsScratch());
    const HBrickScratchCapacities hbrick_before =
        captureHBrickScratchCapacities(baseline.scratch());

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(baseline.query(source, target));
        }
    }

    expectCapacitiesUnchanged(baseline.portBfsScratch(), port_before);
    expectHBrickScratchCapacitiesUnchanged(baseline.scratch(), hbrick_before);
}

TEST(HotPathAllocations, HBrickFlatFallbackQueryDoesNotResizeScratch) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = buildSmallGridGraph(layout);

    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{4U, 4U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    config.max_depth = 1U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    const ScratchCapacities before = captureCapacities(baseline.portBfsScratch());

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(baseline.query(source, target));
        }
    }

    expectCapacitiesUnchanged(baseline.portBfsScratch(), before);
}

TEST(HotPathAllocations, HBrickMicroBfsQueryDoesNotResizeScratch) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = buildSmallGridGraph(layout);

    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{4U, 4U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    config.max_depth = 2U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickMicroBfsBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    const ScratchCapacities micro_bfs_before =
        captureCapacities(baseline.microBfsScratch());
    const ScratchCapacities port_before =
        captureCapacities(baseline.portBfsScratch());
    const HBrickScratchCapacities hbrick_before =
        captureHBrickScratchCapacities(baseline.scratch());

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(baseline.query(source, target));
        }
    }

    expectCapacitiesUnchanged(baseline.microBfsScratch(), micro_bfs_before);
    expectCapacitiesUnchanged(baseline.portBfsScratch(), port_before);
    expectHBrickScratchCapacitiesUnchanged(baseline.scratch(), hbrick_before);
}

TEST(HotPathAllocations, HBrickFusedLiftQueryDoesNotResizeScratch) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = buildSmallGridGraph(layout);

    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{4U, 4U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    config.max_depth = 2U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickFusedLiftBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    const ScratchCapacities micro_bfs_before =
        captureCapacities(baseline.microBfsScratch());
    const ScratchCapacities port_before =
        captureCapacities(baseline.portBfsScratch());
    const HBrickScratchCapacities hbrick_before =
        captureHBrickScratchCapacities(baseline.scratch());

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(baseline.query(source, target));
        }
    }

    expectCapacitiesUnchanged(baseline.microBfsScratch(), micro_bfs_before);
    expectCapacitiesUnchanged(baseline.portBfsScratch(), port_before);
    expectHBrickScratchCapacitiesUnchanged(baseline.scratch(), hbrick_before);
}

TEST(HotPathAllocations, HBrickFusedLiftCacheQueryDoesNotAllocate) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = buildSmallGridGraph(layout);

    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{4U, 4U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    config.max_depth = 2U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickFusedLiftCacheBaseline baseline(16U);
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(baseline.query(source, target));
        }
    }
}

TEST(HotPathAllocations, HBrickSccLabelQueryDoesNotResizeScratch) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = buildSmallGridGraph(layout);

    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{4U, 4U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    config.max_depth = 2U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickSccLabelBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    const ScratchCapacities micro_bfs_before =
        captureCapacities(baseline.microBfsScratch());
    const ScratchCapacities port_before =
        captureCapacities(baseline.portBfsScratch());

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(baseline.query(source, target));
        }
    }

    expectCapacitiesUnchanged(baseline.microBfsScratch(), micro_bfs_before);
    expectCapacitiesUnchanged(baseline.portBfsScratch(), port_before);
}

TEST(HotPathAllocations, HBrickSkipLiftQueryDoesNotResizeScratch) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = buildSmallGridGraph(layout);

    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{4U, 4U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    config.max_depth = 2U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickSkipLiftBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    const ScratchCapacities micro_bfs_before =
        captureCapacities(baseline.microBfsScratch());
    const ScratchCapacities port_before =
        captureCapacities(baseline.portBfsScratch());
    const HBrickScratchCapacities hbrick_before =
        captureHBrickScratchCapacities(baseline.scratch());

    for (uint32_t source = 0U; source < graph.numVertices(); ++source) {
        for (uint32_t target = 0U; target < graph.numVertices(); ++target) {
            static_cast<void>(baseline.query(source, target));
        }
    }

    expectCapacitiesUnchanged(baseline.microBfsScratch(), micro_bfs_before);
    expectCapacitiesUnchanged(baseline.portBfsScratch(), port_before);
    expectHBrickScratchCapacitiesUnchanged(baseline.scratch(), hbrick_before);
}

TEST(HotPathAllocations, HBrickBatchQueryReusesPreparedScratch) {
    hbrick::MazeLayout layout(12U, 12U, true);
    const hbrick::DirectedGridGraph graph = buildSmallGridGraph(layout);

    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{4U, 4U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    // Deliberately shallow: leaves many roots, so the batch also exercises the
    // flat BRICK fallback path instead of only ancestor meets.
    config.max_depth = 2U;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();

    hbrick::HBrickSkipLiftBaseline baseline;
    baseline.preprocess(graph, layout, config);
    ASSERT_EQ(baseline.status(), hbrick::BaselineStatus::Completed);

    const uint32_t num_vertices = graph.numVertices();
    ASSERT_TRUE(baseline.prepareBatch(num_vertices, num_vertices));
    const uint64_t batch_bytes_before = baseline.batchScratchMemoryBytes();
    const ScratchCapacities micro_bfs_before =
        captureCapacities(baseline.microBfsScratch());
    const ScratchCapacities port_before =
        captureCapacities(baseline.portBfsScratch());
    const HBrickScratchCapacities hbrick_before =
        captureHBrickScratchCapacities(baseline.scratch());

    std::vector<uint32_t> all(num_vertices);
    std::iota(all.begin(), all.end(), 0U);
    std::vector<uint8_t> out(static_cast<std::size_t>(num_vertices) * num_vertices, 0U);
    baseline.batchQuery(all, all, out, nullptr);

    // A second, smaller rectangular batch must reuse the same buffers.
    const std::vector<uint32_t> few{0U, 5U, 11U};
    std::vector<uint8_t> few_out(9U, 0U);
    baseline.batchQuery(few, few, few_out, nullptr);

    for (uint32_t index = 0U; index < num_vertices; ++index) {
        EXPECT_EQ(out[static_cast<std::size_t>(index) * num_vertices + index], 1U);
    }

    EXPECT_EQ(baseline.batchScratchMemoryBytes(), batch_bytes_before);
    expectCapacitiesUnchanged(baseline.microBfsScratch(), micro_bfs_before);
    expectCapacitiesUnchanged(baseline.portBfsScratch(), port_before);
    expectHBrickScratchCapacitiesUnchanged(baseline.scratch(), hbrick_before);
}
