#include <gtest/gtest.h>

#include <limits>

#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/preprocess_memory_ledger.hpp"
#include "hbrick/tile/tile_decomposition.hpp"

TEST(PreprocessMemoryLedger, TryChargeTracksExactBytes) {
    hbrick::PreprocessMemoryLedger ledger(100U);
    EXPECT_TRUE(ledger.tryCharge(40U));
    EXPECT_EQ(ledger.chargedBytes(), 40U);
    EXPECT_TRUE(ledger.tryCharge(60U));
    EXPECT_EQ(ledger.chargedBytes(), 100U);
    EXPECT_FALSE(ledger.tryCharge(1U));
    EXPECT_EQ(ledger.chargedBytes(), 100U);
}

TEST(PreprocessMemoryLedger, ReleaseChargeRollsBackFailedStep) {
    hbrick::PreprocessMemoryLedger ledger(100U);
    EXPECT_TRUE(ledger.tryCharge(40U));
    EXPECT_TRUE(ledger.tryCharge(50U));
    ledger.releaseCharge(50U);
    EXPECT_EQ(ledger.chargedBytes(), 40U);
    EXPECT_TRUE(ledger.tryCharge(60U));
    EXPECT_EQ(ledger.chargedBytes(), 100U);
}

TEST(PreprocessMemoryLedger, GlobalCapSkipsSecondTile) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{4U, 4U});

    hbrick::PreprocessMemoryLedger probe(std::numeric_limits<uint64_t>::max());
    const hbrick::BaseTileSummary first_only = hbrick::buildBaseTile(
        graph,
        layout,
        decomposition.slot(0U, 0U),
        probe
    );
    ASSERT_EQ(first_only.status, hbrick::BaselineStatus::Completed);
    ASSERT_GT(probe.chargedBytes(), 0U);

    hbrick::PreprocessMemoryLedger ledger(probe.chargedBytes());
    const hbrick::BaseTileSummary first = hbrick::buildBaseTile(
        graph,
        layout,
        decomposition.slot(0U, 0U),
        ledger
    );
    const hbrick::BaseTileSummary second = hbrick::buildBaseTile(
        graph,
        layout,
        decomposition.slot(1U, 0U),
        ledger
    );

    EXPECT_EQ(first.status, hbrick::BaselineStatus::Completed);
    EXPECT_EQ(second.status, hbrick::BaselineStatus::SkippedByPolicy);
    EXPECT_EQ(ledger.chargedBytes(), probe.chargedBytes());
}
