#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "hbrick/io/movingai_loader.hpp"
#include "hbrick/io/movingai_map.hpp"

namespace {

constexpr const char* kSmallMap =
    "type octile\n"
    "height 3\n"
    "width 4\n"
    "map\n"
    ".G@T\n"
    "SW..\n"
    "....\n";

TEST(MovingAiLoader, ParsesHeaderAndCells) {
    const hbrick::MovingAiLoadResult result = hbrick::parseMovingAiMap(kSmallMap);

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.map.width(), 4U);
    EXPECT_EQ(result.map.height(), 3U);
    EXPECT_EQ(result.map.typeName(), "octile");
    EXPECT_EQ(result.map.cellAt({0U, 0U}), '.');
    EXPECT_EQ(result.map.cellAt({1U, 0U}), 'G');
    EXPECT_EQ(result.map.cellAt({2U, 0U}), '@');
    EXPECT_EQ(result.map.cellAt({3U, 0U}), 'T');
    EXPECT_EQ(result.map.cellAt({0U, 1U}), 'S');
    EXPECT_EQ(result.map.cellAt({1U, 1U}), 'W');
}

TEST(MovingAiLoader, ToleratesCarriageReturnsAndWidthBeforeHeight) {
    const std::string text =
        "type octile\r\n"
        "width 2\r\n"
        "height 2\r\n"
        "map\r\n"
        "..\r\n"
        ".@\r\n";

    const hbrick::MovingAiLoadResult result = hbrick::parseMovingAiMap(text);

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.map.width(), 2U);
    EXPECT_EQ(result.map.height(), 2U);
    EXPECT_EQ(result.map.cellAt({1U, 1U}), '@');
}

TEST(MovingAiLoader, RejectsMissingMapMarker) {
    const hbrick::MovingAiLoadResult result =
        hbrick::parseMovingAiMap("type octile\nheight 2\nwidth 2\n..\n..\n");

    EXPECT_FALSE(result.ok());
    EXPECT_NE(result.error.find("header"), std::string::npos);
}

TEST(MovingAiLoader, RejectsMissingDimensions) {
    const hbrick::MovingAiLoadResult result =
        hbrick::parseMovingAiMap("type octile\nmap\n..\n");

    EXPECT_FALSE(result.ok());
}

TEST(MovingAiLoader, RejectsShortRows) {
    const hbrick::MovingAiLoadResult result =
        hbrick::parseMovingAiMap("height 2\nwidth 4\nmap\n....\n..\n");

    EXPECT_FALSE(result.ok());
    EXPECT_NE(result.error.find("row 1"), std::string::npos);
}

TEST(MovingAiLoader, RejectsTruncatedGrid) {
    const hbrick::MovingAiLoadResult result =
        hbrick::parseMovingAiMap("height 3\nwidth 2\nmap\n..\n..\n");

    EXPECT_FALSE(result.ok());
}

TEST(MovingAiLoader, RejectsOverflowingCellCount) {
    const hbrick::MovingAiLoadResult result = hbrick::parseMovingAiMap(
        "type octile\n"
        "width 65536\n"
        "height 65536\n"
        "map\n"
        "..\n"
        "..\n"
    );

    EXPECT_FALSE(result.ok());
    EXPECT_NE(result.error.find("overflow"), std::string::npos);
}

TEST(MovingAiLoader, ReportsMissingFile) {
    const hbrick::MovingAiLoadResult result =
        hbrick::loadMovingAiMap("/nonexistent/path/arena.map");

    EXPECT_FALSE(result.ok());
}

TEST(MovingAiTerrain, ClassifiesDocumentedVocabulary) {
    using hbrick::MovingAiTerrain;
    EXPECT_EQ(hbrick::movingAiTerrainFromChar('.'), MovingAiTerrain::Ground);
    EXPECT_EQ(hbrick::movingAiTerrainFromChar('G'), MovingAiTerrain::Ground);
    EXPECT_EQ(hbrick::movingAiTerrainFromChar('T'), MovingAiTerrain::Tree);
    EXPECT_EQ(hbrick::movingAiTerrainFromChar('S'), MovingAiTerrain::Swamp);
    EXPECT_EQ(hbrick::movingAiTerrainFromChar('W'), MovingAiTerrain::Water);
    EXPECT_EQ(hbrick::movingAiTerrainFromChar('@'), MovingAiTerrain::OutOfBounds);
    EXPECT_EQ(hbrick::movingAiTerrainFromChar('O'), MovingAiTerrain::OutOfBounds);
    EXPECT_EQ(hbrick::movingAiTerrainFromChar('B'), MovingAiTerrain::WeightedTerrain);
    EXPECT_EQ(hbrick::movingAiTerrainFromChar('#'), MovingAiTerrain::Unknown);
}

TEST(MovingAiPassability, PoliciesWidenMonotonically) {
    using Policy = hbrick::MovingAiPassabilityPolicy;

    EXPECT_TRUE(hbrick::isMovingAiCellPassable('.', Policy::GroundOnly));
    EXPECT_FALSE(hbrick::isMovingAiCellPassable('S', Policy::GroundOnly));
    EXPECT_TRUE(hbrick::isMovingAiCellPassable('S', Policy::GroundAndSwamp));
    EXPECT_FALSE(hbrick::isMovingAiCellPassable('W', Policy::GroundAndSwamp));
    EXPECT_TRUE(hbrick::isMovingAiCellPassable('W', Policy::AllTraversable));
    EXPECT_FALSE(hbrick::isMovingAiCellPassable('B', Policy::AllTraversable));
    EXPECT_TRUE(hbrick::isMovingAiCellPassable('B', Policy::AnyTerrainLetter));
    EXPECT_FALSE(hbrick::isMovingAiCellPassable('T', Policy::AnyTerrainLetter));
    EXPECT_FALSE(hbrick::isMovingAiCellPassable('@', Policy::AnyTerrainLetter));
}

TEST(MovingAiMap, ToMazeLayoutAppliesPolicy) {
    const hbrick::MovingAiLoadResult result = hbrick::parseMovingAiMap(kSmallMap);
    ASSERT_TRUE(result.ok()) << result.error;

    const hbrick::MazeLayout strict =
        result.map.toMazeLayout(hbrick::MovingAiPassabilityPolicy::GroundOnly);
    EXPECT_EQ(strict.width(), 4U);
    EXPECT_EQ(strict.height(), 3U);
    EXPECT_TRUE(strict.isPassable(0U, 0U));
    EXPECT_TRUE(strict.isPassable(1U, 0U));
    EXPECT_FALSE(strict.isPassable(2U, 0U));
    EXPECT_FALSE(strict.isPassable(3U, 0U));
    EXPECT_FALSE(strict.isPassable(0U, 1U));
    EXPECT_FALSE(strict.isPassable(1U, 1U));
    EXPECT_EQ(strict.passableCount(), 8U);

    const hbrick::MazeLayout wide =
        result.map.toMazeLayout(hbrick::MovingAiPassabilityPolicy::AllTraversable);
    EXPECT_TRUE(wide.isPassable(0U, 1U));
    EXPECT_TRUE(wide.isPassable(1U, 1U));
    EXPECT_EQ(wide.passableCount(), 10U);
}

TEST(MovingAiMap, CellAtOutOfBoundsReturnsOutOfBounds) {
    const hbrick::MovingAiLoadResult result = hbrick::parseMovingAiMap(kSmallMap);
    ASSERT_TRUE(result.ok()) << result.error;

    EXPECT_EQ(result.map.cellAt({4U, 0U}), '@');
    EXPECT_EQ(result.map.terrainAt({0U, 3U}), hbrick::MovingAiTerrain::OutOfBounds);
}

TEST(MovingAiMap, RejectsMismatchedCellBuffer) {
    EXPECT_THROW(
        hbrick::MovingAiMap(
            hbrick::GridDimensions{2U, 2U},
            "octile",
            std::vector<char>{'.', '.'}
        ),
        std::invalid_argument
    );
}

// Round-trips real benchmark files when the local dataset archive is present.
TEST(MovingAiLoader, LoadsRealDatasetMapsWhenAvailable) {
    const std::filesystem::path root = HBRICK_SOURCE_DIR;
    const std::filesystem::path extracted = root / "datasets" / "movingai" / "extracted";
    if (!std::filesystem::exists(extracted)) {
        GTEST_SKIP() << "datasets/movingai/extracted not present; "
                        "run datasets/movingai/fetch_movingai.sh --extract-only";
    }

    const std::filesystem::path arena = extracted / "dao" / "maps" / "arena.map";
    ASSERT_TRUE(std::filesystem::exists(arena));

    const hbrick::MovingAiLoadResult result = hbrick::loadMovingAiMap(arena);
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.map.width(), 49U);
    EXPECT_EQ(result.map.height(), 49U);
    EXPECT_EQ(result.map.typeName(), "octile");

    const hbrick::MazeLayout layout =
        result.map.toMazeLayout(hbrick::MovingAiPassabilityPolicy::GroundOnly);
    EXPECT_GT(layout.passableCount(), 0U);
    EXPECT_LT(layout.passableCount(), layout.numVertices());
}

}  // namespace
