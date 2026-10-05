#include <gtest/gtest.h>

#include "hbrick/tile/group_size.hpp"
#include "hbrick/tile/tile_size.hpp"

TEST(TileSize, RejectsTooSmallDimensions) {
    EXPECT_FALSE((hbrick::TileSize{0U, 0U}.isValid()));
    EXPECT_FALSE((hbrick::TileSize{1U, 4U}.isValid()));
    EXPECT_FALSE((hbrick::TileSize{4U, 1U}.isValid()));
    EXPECT_FALSE((hbrick::TileSize{1U, 1U}.isValid()));
}

TEST(TileSize, AcceptsValidDimensions) {
    EXPECT_TRUE((hbrick::TileSize{2U, 2U}.isValid()));
    EXPECT_TRUE((hbrick::TileSize{4U, 8U}.isValid()));
    EXPECT_TRUE((hbrick::TileSize{8U, 4U}.isValid()));
}

TEST(TileSize, Equality) {
    EXPECT_EQ((hbrick::TileSize{4U, 8U}), (hbrick::TileSize{4U, 8U}));
    EXPECT_NE((hbrick::TileSize{4U, 8U}), (hbrick::TileSize{8U, 4U}));
}

TEST(GroupSize, RejectsUnitOrSmallerAxes) {
    EXPECT_FALSE((hbrick::GroupSize{0U, 0U}.isValid()));
    EXPECT_FALSE((hbrick::GroupSize{1U, 1U}.isValid()));
    EXPECT_FALSE((hbrick::GroupSize{1U, 2U}.isValid()));
    EXPECT_FALSE((hbrick::GroupSize{2U, 1U}.isValid()));
    EXPECT_FALSE((hbrick::GroupSize{4U, 1U}.isValid()));
}

TEST(GroupSize, AcceptsAtLeastTwoByTwo) {
    EXPECT_TRUE((hbrick::GroupSize{}.isValid()));
    EXPECT_TRUE((hbrick::GroupSize{2U, 2U}.isValid()));
    EXPECT_TRUE((hbrick::GroupSize{2U, 4U}.isValid()));
    EXPECT_TRUE((hbrick::GroupSize{3U, 3U}.isValid()));
}

TEST(GroupSize, Equality) {
    EXPECT_EQ((hbrick::GroupSize{2U, 4U}), (hbrick::GroupSize{2U, 4U}));
    EXPECT_NE((hbrick::GroupSize{2U, 4U}), (hbrick::GroupSize{4U, 2U}));
}
