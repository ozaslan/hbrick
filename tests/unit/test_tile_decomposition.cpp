#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/tile/tile_decomposition.hpp"
#include "hbrick/tile/tile_port.hpp"

namespace {

std::string coordKey(hbrick::GridCoord coord) {
    return std::to_string(coord.x) + "," + std::to_string(coord.y);
}

void expectFullCoverageNoOverlap(const hbrick::TileDecomposition& decomposition) {
    std::set<std::string> owned;
    for (uint32_t tile_j = 0U; tile_j < decomposition.numSlotsJ(); ++tile_j) {
        for (uint32_t tile_i = 0U; tile_i < decomposition.numSlotsI(); ++tile_i) {
            const hbrick::TileSlot& slot = decomposition.slot(tile_i, tile_j);
            EXPECT_GE(slot.extent.width, 2U);
            EXPECT_GE(slot.extent.height, 2U);

            for (uint32_t y = slot.origin.y; y < slot.maxY(); ++y) {
                for (uint32_t x = slot.origin.x; x < slot.maxX(); ++x) {
                    const hbrick::GridCoord coord{x, y};
                    const auto insert_result = owned.insert(coordKey(coord));
                    EXPECT_TRUE(insert_result.second)
                        << "duplicate ownership at (" << x << "," << y << ")";
                }
            }
        }
    }

    EXPECT_EQ(owned.size(), static_cast<std::size_t>(decomposition.mapWidth()) *
                                static_cast<std::size_t>(decomposition.mapHeight()));
}

}  // namespace

TEST(TileDecomposition, EightByEightFourByFourTiles) {
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{4U, 4U});

    EXPECT_EQ(decomposition.numSlotsI(), 2U);
    EXPECT_EQ(decomposition.numSlotsJ(), 2U);
    EXPECT_EQ(decomposition.numSlots(), 4U);

    for (uint32_t tile_j = 0U; tile_j < 2U; ++tile_j) {
        for (uint32_t tile_i = 0U; tile_i < 2U; ++tile_i) {
            const hbrick::TileSlot& slot = decomposition.slot(tile_i, tile_j);
            EXPECT_EQ(slot.extent.width, 4U);
            EXPECT_EQ(slot.extent.height, 4U);
            EXPECT_EQ(slot.origin.x, tile_i * 4U);
            EXPECT_EQ(slot.origin.y, tile_j * 4U);
        }
    }

    expectFullCoverageNoOverlap(decomposition);
}

TEST(TileDecomposition, NineBySevenMergesRemainderStrip) {
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(9U, 7U, hbrick::TileSize{4U, 4U});

    EXPECT_EQ(decomposition.numSlotsI(), 2U);
    EXPECT_EQ(decomposition.numSlotsJ(), 2U);

    const hbrick::TileSlot& top_right = decomposition.slot(1U, 0U);
    EXPECT_EQ(top_right.origin.x, 4U);
    EXPECT_EQ(top_right.extent.width, 5U);
    EXPECT_EQ(top_right.extent.height, 4U);

    const hbrick::TileSlot& bottom_left = decomposition.slot(0U, 1U);
    EXPECT_EQ(bottom_left.origin.y, 4U);
    EXPECT_EQ(bottom_left.extent.width, 4U);
    EXPECT_EQ(bottom_left.extent.height, 3U);

    expectFullCoverageNoOverlap(decomposition);
}

TEST(TileDecomposition, FiveByFourSingleMergedSlot) {
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(5U, 4U, hbrick::TileSize{4U, 4U});

    EXPECT_EQ(decomposition.numSlotsI(), 1U);
    EXPECT_EQ(decomposition.numSlotsJ(), 1U);
    EXPECT_EQ(decomposition.slot(0U, 0U).extent.width, 5U);
    EXPECT_EQ(decomposition.slot(0U, 0U).extent.height, 4U);

    expectFullCoverageNoOverlap(decomposition);
}

TEST(TileDecomposition, CornerOwnedByExactlyOneSlot) {
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{4U, 4U});

    const hbrick::GridCoord corner{3U, 3U};
    hbrick::TileSlotId slot_id{};
    ASSERT_TRUE(decomposition.slotAt(corner, slot_id));
    EXPECT_EQ(slot_id.tile_i, 0U);
    EXPECT_EQ(slot_id.tile_j, 0U);
    EXPECT_EQ(slot_id.index, 0U);
}

TEST(TileDecomposition, SlotLookupRoundTrip) {
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{4U, 4U});

    const std::vector<hbrick::GridCoord> coords{
        {0U, 0U},
        {3U, 3U},
        {4U, 4U},
        {7U, 7U},
        {2U, 5U},
    };

    for (const hbrick::GridCoord coord : coords) {
        hbrick::TileSlotId slot_id{};
        ASSERT_TRUE(decomposition.slotAt(coord, slot_id));
        EXPECT_EQ(decomposition.slotIndexAt(coord), slot_id.index);
        EXPECT_TRUE(decomposition.slot(slot_id.tile_i, slot_id.tile_j).contains(coord));
    }

    EXPECT_EQ(
        decomposition.slotIndexAt(hbrick::GridCoord{99U, 0U}),
        std::numeric_limits<uint32_t>::max()
    );
}

TEST(TileDecomposition, PerimeterDetection) {
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{4U, 4U});

    const hbrick::TileSlot& slot = decomposition.slot(0U, 0U);
    EXPECT_TRUE(hbrick::isOnTilePerimeter(hbrick::GridCoord{0U, 0U}, slot));
    EXPECT_TRUE(hbrick::isOnTilePerimeter(hbrick::GridCoord{3U, 1U}, slot));
    EXPECT_TRUE(hbrick::isOnTilePerimeter(hbrick::GridCoord{1U, 3U}, slot));
    EXPECT_FALSE(hbrick::isOnTilePerimeter(hbrick::GridCoord{1U, 1U}, slot));
    EXPECT_FALSE(hbrick::isOnTilePerimeter(hbrick::GridCoord{2U, 2U}, slot));
}

TEST(TileDecomposition, PortSideCornerPrecedence) {
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{4U, 4U});

    const hbrick::TileSlot& slot = decomposition.slot(0U, 0U);
    EXPECT_EQ(hbrick::portSideForCoord(hbrick::GridCoord{0U, 0U}, slot), hbrick::PortSide::North);
    EXPECT_EQ(hbrick::portSideForCoord(hbrick::GridCoord{3U, 0U}, slot), hbrick::PortSide::North);
    EXPECT_EQ(hbrick::portSideForCoord(hbrick::GridCoord{3U, 3U}, slot), hbrick::PortSide::East);
    EXPECT_EQ(hbrick::portSideForCoord(hbrick::GridCoord{0U, 3U}, slot), hbrick::PortSide::South);
    EXPECT_EQ(hbrick::portSideForCoord(hbrick::GridCoord{0U, 1U}, slot), hbrick::PortSide::West);
}

TEST(TileDecomposition, RejectsInvalidInputs) {
    EXPECT_THROW(
        (void)hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{1U, 4U}),
        std::invalid_argument
    );
    EXPECT_THROW(
        (void)hbrick::TileDecomposition::decompose(1U, 8U, hbrick::TileSize{4U, 4U}),
        std::invalid_argument
    );
    EXPECT_THROW(
        (void)hbrick::TileDecomposition::decompose(8U, 1U, hbrick::TileSize{4U, 4U}),
        std::invalid_argument
    );
}
