/**
 * @file test_baseline_memory_accounting.cpp
 * @brief Unit tests for memory accounting and budget cap enforcement in H-BRICK variants.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/baselines/hbrick_fused_lift_baseline.hpp"
#include "hbrick/baselines/hbrick_fused_lift_cache_baseline.hpp"
#include "hbrick/baselines/hbrick_scc_label_baseline.hpp"
#include "hbrick/baselines/hbrick_skip_lift_baseline.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

hbrick::HBrickConfig makeConfig(
    const uint64_t max_memory_bytes = std::numeric_limits<uint64_t>::max()
) {
    hbrick::HBrickConfig config{};
    config.base_tile_size = hbrick::TileSize{4U, 4U};
    config.group_size = hbrick::GroupSize{2U, 2U};
    config.max_depth = 2U;
    config.max_memory_bytes = max_memory_bytes;
    return config;
}

struct SampleFixture {
    hbrick::MazeLayout layout{16U, 16U};
    hbrick::DirectedGridGraph graph;

    SampleFixture() {
        layout.setPassable(hbrick::GridCoord{3U, 3U}, false);
        layout.setPassable(hbrick::GridCoord{7U, 7U}, false);
        layout.setPassable(hbrick::GridCoord{11U, 11U}, false);
        graph = hbrick::DirectedGridGraphBuilder::build(
            layout,
            hbrick::GridEdgeConversionMode::BidirectionalAll
        );
    }
};

}  // namespace

TEST(BaselineMemoryAccounting, EndpointLiftCacheMemoryAccounting) {
    const SampleFixture fixture;
    const auto config = makeConfig();

    hbrick::HBrickIndex index = hbrick::HBrickIndex::build(fixture.graph, fixture.layout, config);
    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);

    hbrick::EndpointLiftCache empty_cache;
    EXPECT_EQ(empty_cache.memoryBytes(), sizeof(hbrick::EndpointLiftCache));

    hbrick::EndpointLiftCache cache512;
    cache512.init(512U, index);
    const uint64_t bytes512 = cache512.memoryBytes();
    EXPECT_GT(bytes512, sizeof(hbrick::EndpointLiftCache));

    hbrick::EndpointLiftCache cache1024;
    cache1024.init(1024U, index);
    const uint64_t bytes1024 = cache1024.memoryBytes();
    EXPECT_GT(bytes1024, bytes512);

    cache512.clear();
    EXPECT_EQ(cache512.memoryBytes(), bytes512);
}

TEST(BaselineMemoryAccounting, FusedLiftCacheIncludesCacheInStorageBytes) {
    const SampleFixture fixture;
    const auto config = makeConfig();

    hbrick::HBrickFusedLiftBaseline fused;
    fused.preprocess(fixture.graph, fixture.layout, config);
    ASSERT_EQ(fused.status(), hbrick::BaselineStatus::Completed);

    hbrick::HBrickFusedLiftCacheBaseline cached(512U);
    cached.preprocess(fixture.graph, fixture.layout, config);
    ASSERT_EQ(cached.status(), hbrick::BaselineStatus::Completed);

    EXPECT_GT(cached.endpointCacheMemoryBytes(), 0U);
    EXPECT_EQ(
        cached.indexStorageBytes(),
        cached.baseIndexStorageBytes()
            + cached.fusedLiftsMemoryBytes()
            + cached.endpointCacheMemoryBytes()
    );
    EXPECT_EQ(
        cached.indexStorageBytes(),
        fused.indexStorageBytes() + cached.endpointCacheMemoryBytes()
    );

    const uint64_t initial_bytes = cached.indexStorageBytes();
    (void)cached.query(0U, 15U);
    (void)cached.query(5U, 120U);
    EXPECT_EQ(cached.indexStorageBytes(), initial_bytes);

    cached.clearCache();
    EXPECT_EQ(cached.indexStorageBytes(), initial_bytes);
}

TEST(BaselineMemoryAccounting, SccLabelMemoryAccountingAndBreakdown) {
    const SampleFixture fixture;
    const auto config = makeConfig();

    hbrick::HBrickSccLabelBaseline scc;
    scc.preprocess(fixture.graph, fixture.layout, config);
    ASSERT_EQ(scc.status(), hbrick::BaselineStatus::Completed);

    EXPECT_GT(scc.sccLabelsMemoryBytes(), 0U);
    EXPECT_EQ(
        scc.indexStorageBytes(),
        scc.baseIndexStorageBytes() + scc.sccLabelsMemoryBytes()
    );
}

TEST(BaselineMemoryAccounting, MemoryBudgetEnforcementRejectsOverBudgetAuxiliary) {
    const SampleFixture fixture;

    hbrick::HBrickIndex probe_index =
        hbrick::HBrickIndex::build(fixture.graph, fixture.layout, makeConfig());
    ASSERT_EQ(probe_index.status(), hbrick::BaselineStatus::Completed);
    const uint64_t base_bytes = probe_index.measureStorageBytes();

    // Cap allows base HBrickIndex, but is strictly less than what auxiliary indices require.
    const uint64_t tight_cap = base_bytes + 16U;
    auto tight_config = makeConfig(tight_cap);

    // 1. FusedLift
    {
        hbrick::HBrickFusedLiftBaseline fused;
        fused.preprocess(fixture.graph, fixture.layout, tight_config);
        EXPECT_EQ(fused.status(), hbrick::BaselineStatus::OutOfMemory);
        EXPECT_FALSE(fused.fusedLifts().isValid());
        EXPECT_EQ(fused.query(0U, 1U), hbrick::ReachabilityAnswer::Unreachable);
    }

    // 2. FusedLiftCache
    {
        hbrick::HBrickFusedLiftCacheBaseline cached(512U);
        cached.preprocess(fixture.graph, fixture.layout, tight_config);
        EXPECT_EQ(cached.status(), hbrick::BaselineStatus::OutOfMemory);
        EXPECT_EQ(cached.query(0U, 1U), hbrick::ReachabilityAnswer::Unreachable);
    }

    // 3. SkipLift
    {
        hbrick::HBrickSkipLiftBaseline skip;
        skip.preprocess(fixture.graph, fixture.layout, tight_config);
        EXPECT_EQ(skip.status(), hbrick::BaselineStatus::OutOfMemory);
        EXPECT_FALSE(skip.skipLifts().isValid());
        EXPECT_EQ(skip.query(0U, 1U), hbrick::ReachabilityAnswer::Unreachable);
    }

    // 4. SccLabel
    {
        hbrick::HBrickSccLabelBaseline scc;
        scc.preprocess(fixture.graph, fixture.layout, tight_config);
        EXPECT_EQ(scc.status(), hbrick::BaselineStatus::OutOfMemory);
        EXPECT_EQ(scc.query(0U, 1U), hbrick::ReachabilityAnswer::Unreachable);
    }
}
