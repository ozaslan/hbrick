#include <gtest/gtest.h>

#include <filesystem>

#include "hbrick/bench/balanced_query_workload.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/passable_vertex_map.hpp"
#include "hbrick/grid/maze_layout.hpp"

TEST(BalancedQueryWorkload, FillsSixBucketsOnAcyclicGrid) {
    const hbrick::MazeLayout layout(16U, 16U, true);
    const hbrick::DirectedGridGraph grid = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::AcyclicEastSouth
    );
    const hbrick::PassableVertexMap map = hbrick::PassableVertexMap::fromLayout(layout);
    const hbrick::CsrGraph compact = hbrick::inducePassableCsr(grid, map);

    hbrick::BalancedWorkloadSpec spec{};
    spec.query_count = 60U;
    spec.seed = 0xABCDEULL;
    spec.fail_if_shortfall = true;

    const hbrick::BalancedWorkload workload =
        hbrick::generateBalancedWorkload(layout, compact, map, spec);
    ASSERT_TRUE(workload.complete);
    EXPECT_EQ(workload.pairs.size(), 60U);
    EXPECT_EQ(workload.manhattan_diameter, 30U);

    uint32_t counts[6]{};
    for (const hbrick::ReachabilityQueryPair& pair : workload.pairs) {
        ASSERT_LE(pair.range_class, 2U);
        ASSERT_LE(pair.polarity, 1U);
        ++counts[static_cast<uint32_t>(pair.range_class) * 2U
            + (pair.polarity == 1U ? 0U : 1U)];
        EXPECT_TRUE(layout.isPassable(hbrick::VertexId{pair.source}));
        EXPECT_TRUE(layout.isPassable(hbrick::VertexId{pair.target}));
    }
    for (uint32_t bucket = 0U; bucket < 6U; ++bucket) {
        EXPECT_EQ(counts[bucket], 10U) << bucket;
        EXPECT_EQ(workload.filled_per_bucket[bucket], 10U) << bucket;
    }
}

TEST(BalancedQueryWorkload, RoundTripCsvPreservesPairs) {
    const hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph grid = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::AcyclicEastSouth
    );
    const hbrick::PassableVertexMap map = hbrick::PassableVertexMap::fromLayout(layout);
    const hbrick::CsrGraph compact = hbrick::inducePassableCsr(grid, map);

    hbrick::BalancedWorkloadSpec spec{};
    spec.query_count = 24U;
    spec.fail_if_shortfall = false;
    const hbrick::BalancedWorkload original =
        hbrick::generateBalancedWorkload(layout, compact, map, spec);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hbrick_balanced_workload.csv";
    std::string error;
    ASSERT_TRUE(hbrick::writeBalancedWorkloadCsv(path, original, error)) << error;

    hbrick::BalancedWorkload loaded;
    ASSERT_TRUE(hbrick::readBalancedWorkloadCsv(path, loaded, error)) << error;
    ASSERT_EQ(loaded.pairs.size(), original.pairs.size());
    EXPECT_EQ(loaded.pair_list_hash, original.pair_list_hash);
    for (std::size_t index = 0; index < original.pairs.size(); ++index) {
        EXPECT_EQ(loaded.pairs[index].source, original.pairs[index].source);
        EXPECT_EQ(loaded.pairs[index].target, original.pairs[index].target);
        EXPECT_EQ(loaded.pairs[index].range_class, original.pairs[index].range_class);
        EXPECT_EQ(loaded.pairs[index].polarity, original.pairs[index].polarity);
    }
    std::filesystem::remove(path);
}

TEST(BalancedQueryWorkload, PolaritySplitFillsBothSidesOnAcyclicGrid) {
    const hbrick::MazeLayout layout(12U, 12U, true);
    const hbrick::DirectedGridGraph grid = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::AcyclicEastSouth
    );
    const hbrick::PassableVertexMap map = hbrick::PassableVertexMap::fromLayout(layout);
    const hbrick::CsrGraph compact = hbrick::inducePassableCsr(grid, map);

    hbrick::PolaritySplitWorkloadSpec spec{};
    spec.positive_count = 32U;
    spec.negative_count = 32U;
    spec.seed = 0x515EEDULL;
    spec.fail_if_shortfall = true;

    const hbrick::PolaritySplitWorkload workload =
        hbrick::generatePolaritySplitWorkload(compact, map, spec);
    ASSERT_TRUE(workload.complete);
    EXPECT_EQ(workload.pairs.size(), 64U);
    EXPECT_EQ(workload.filled_positive, 32U);
    EXPECT_EQ(workload.filled_negative, 32U);

    uint32_t positives = 0U;
    uint32_t negatives = 0U;
    for (const hbrick::ReachabilityQueryPair& pair : workload.pairs) {
        ASSERT_LE(pair.polarity, 1U);
        if (pair.polarity == 1U) {
            ++positives;
        } else {
            ++negatives;
        }
    }
    EXPECT_EQ(positives, 32U);
    EXPECT_EQ(negatives, 32U);
}

TEST(BalancedQueryWorkload, PolaritySplitFailsWhenGraphIsStronglyConnected) {
    const hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph grid = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::PassableVertexMap map = hbrick::PassableVertexMap::fromLayout(layout);
    const hbrick::CsrGraph compact = hbrick::inducePassableCsr(grid, map);

    hbrick::PolaritySplitWorkloadSpec spec{};
    spec.positive_count = 16U;
    spec.negative_count = 16U;
    spec.fail_if_shortfall = true;

    const hbrick::PolaritySplitWorkload workload =
        hbrick::generatePolaritySplitWorkload(compact, map, spec);
    EXPECT_FALSE(workload.complete);
    EXPECT_TRUE(workload.pairs.empty());
    EXPECT_EQ(workload.filled_negative, 0U);
}

TEST(BalancedQueryWorkload, ShortfallClearsPairsWhenRequired) {
    const hbrick::MazeLayout layout(3U, 3U, true);
    const hbrick::DirectedGridGraph grid = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::PassableVertexMap map = hbrick::PassableVertexMap::fromLayout(layout);
    const hbrick::CsrGraph compact = hbrick::inducePassableCsr(grid, map);

    hbrick::BalancedWorkloadSpec spec{};
    spec.query_count = 60U;
    spec.fail_if_shortfall = true;
    const hbrick::BalancedWorkload workload =
        hbrick::generateBalancedWorkload(layout, compact, map, spec);
    EXPECT_FALSE(workload.complete);
    EXPECT_TRUE(workload.pairs.empty());
}
