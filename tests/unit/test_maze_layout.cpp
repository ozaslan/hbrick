#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "hbrick/grid/direction.hpp"
#include "hbrick/grid/grid_dimensions.hpp"
#include "hbrick/grid/passable_grid.hpp"

namespace {

struct RecordedPair {
    hbrick::GridCoord from;
    hbrick::GridCoord to;
    hbrick::Direction direction;
};

std::string pairKey(const RecordedPair& pair) {
    return std::to_string(pair.from.x) + "," + std::to_string(pair.from.y) + "->" +
           std::to_string(pair.to.x) + "," + std::to_string(pair.to.y) + ":" +
           std::to_string(static_cast<uint8_t>(pair.direction));
}

}  // namespace

TEST(PassableGrid, DimensionsAndPassableCount) {
    const hbrick::PassableGrid grid(4U, 3U);
    EXPECT_EQ(grid.width(), 4U);
    EXPECT_EQ(grid.height(), 3U);
    EXPECT_EQ(grid.numVertices(), 12U);
    EXPECT_TRUE(grid.dimensions().isValid());
    EXPECT_EQ(grid.passableCount(), 12U);
}

TEST(PassableGrid, SetAndQueryPassability) {
    hbrick::PassableGrid grid(3U, 2U);
    const hbrick::GridCoord blocked{1U, 0U};

    EXPECT_TRUE(grid.isPassable(blocked));
    grid.setPassable(blocked, false);
    EXPECT_FALSE(grid.isPassable(blocked));
    EXPECT_FALSE(grid.isPassable(hbrick::VertexId{blocked.linearIndex(grid.width())}));
    EXPECT_EQ(grid.passableCount(), 5U);
}

TEST(PassableGrid, VertexIdRoundTrip) {
    const hbrick::PassableGrid grid(5U, 4U);
    const hbrick::GridCoord coord{3U, 2U};
    const hbrick::VertexId vertex = grid.vertexId(coord);

    EXPECT_TRUE(vertex.isValid());
    EXPECT_EQ(vertex.value, coord.linearIndex(grid.width()));
    EXPECT_EQ(grid.coordFromVertex(vertex), coord);
}

TEST(PassableGrid, TryNeighborRespectsBounds) {
    const hbrick::PassableGrid grid(2U, 2U);
    hbrick::GridCoord neighbor{};

    EXPECT_TRUE(grid.tryNeighbor(hbrick::GridCoord{0U, 0U}, hbrick::Direction::East, neighbor));
    EXPECT_EQ(neighbor, hbrick::GridCoord(1U, 0U));

    EXPECT_FALSE(grid.tryNeighbor(hbrick::GridCoord{1U, 0U}, hbrick::Direction::East, neighbor));
    EXPECT_TRUE(grid.tryNeighbor(hbrick::GridCoord{0U, 0U}, hbrick::Direction::South, neighbor));
    EXPECT_EQ(neighbor, hbrick::GridCoord(0U, 1U));
}

TEST(PassableGrid, EastSouthPairEnumerationIsRowMajor) {
    const hbrick::PassableGrid grid(3U, 2U);
    std::vector<RecordedPair> pairs;

    grid.forEachPassableAdjacentPairEastSouth([&pairs](
        const hbrick::GridCoord from,
        const hbrick::GridCoord to,
        const hbrick::Direction direction
    ) {
        pairs.push_back(RecordedPair{from, to, direction});
    });

    ASSERT_EQ(pairs.size(), 7U);

    const std::vector<std::string> expected{
        "0,0->1,0:0",
        "0,0->0,1:1",
        "1,0->2,0:0",
        "1,0->1,1:1",
        "2,0->2,1:1",
        "0,1->1,1:0",
        "1,1->2,1:0",
    };

    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(pairKey(pairs[i]), expected[i]) << "pair index " << i;
    }
}

TEST(PassableGrid, ImpassableCellsBreakAdjacentPairs) {
    hbrick::PassableGrid grid(3U, 2U);
    grid.setPassable(hbrick::GridCoord{1U, 0U}, false);

    std::vector<RecordedPair> pairs;
    grid.forEachPassableAdjacentPairEastSouth([&pairs](
        const hbrick::GridCoord from,
        const hbrick::GridCoord to,
        const hbrick::Direction direction
    ) {
        pairs.push_back(RecordedPair{from, to, direction});
    });

    ASSERT_EQ(pairs.size(), 4U);
    EXPECT_EQ(pairKey(pairs[0]), "0,0->0,1:1");
    EXPECT_EQ(pairKey(pairs[1]), "2,0->2,1:1");
    EXPECT_EQ(pairKey(pairs[2]), "0,1->1,1:0");
    EXPECT_EQ(pairKey(pairs[3]), "1,1->2,1:0");
}

TEST(PassableGrid, ZeroSizeGridHasNoVertices) {
    const hbrick::PassableGrid grid(0U, 0U);
    EXPECT_EQ(grid.numVertices(), 0U);
    EXPECT_EQ(grid.passableCount(), 0U);
    EXPECT_FALSE(grid.dimensions().isValid());

    std::size_t pair_count = 0;
    grid.forEachPassableAdjacentPairEastSouth([&pair_count](
        hbrick::GridCoord,
        hbrick::GridCoord,
        hbrick::Direction
    ) {
        ++pair_count;
    });
    EXPECT_EQ(pair_count, 0U);
}

TEST(GridDimensions, CoordFromLinearIndexMatchesRowMajor) {
    EXPECT_EQ(hbrick::coordFromLinearIndex(5U, 7U), hbrick::GridCoord(2U, 1U));
    EXPECT_EQ(hbrick::coordFromLinearIndex(0U, 3U), hbrick::GridCoord(0U, 0U));
}

TEST(Direction, DeltasMatchCardinalDirections) {
    EXPECT_EQ(hbrick::directionDeltaX(hbrick::Direction::East), 1);
    EXPECT_EQ(hbrick::directionDeltaY(hbrick::Direction::South), 1);
    EXPECT_EQ(hbrick::directionDeltaX(hbrick::Direction::West), -1);
    EXPECT_EQ(hbrick::directionDeltaY(hbrick::Direction::North), -1);
}
