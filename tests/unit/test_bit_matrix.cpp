#include <gtest/gtest.h>

#include "hbrick/bit/bit_matrix.hpp"

TEST(BitMatrix, StoresNonMultipleOf64Columns) {
    hbrick::BitMatrix matrix(3U, 70U);

    EXPECT_EQ(matrix.numRows(), 3U);
    EXPECT_EQ(matrix.numCols(), 70U);
    EXPECT_EQ(matrix.row(0U).numWords(), 2U);
    EXPECT_EQ(matrix.memoryBytes(), 3U * 2U * sizeof(uint64_t));

    matrix.set(1U, 69U);
    EXPECT_TRUE(matrix.test(1U, 69U));
    EXPECT_FALSE(matrix.test(0U, 69U));
}

TEST(BitMatrix, ZeroSizeMatrixHasNoRows) {
    hbrick::BitMatrix matrix(0U, 0U);

    EXPECT_EQ(matrix.numRows(), 0U);
    EXPECT_EQ(matrix.numCols(), 0U);
    EXPECT_EQ(matrix.memoryBytes(), 0U);
    EXPECT_FALSE(matrix.test(0U, 0U));
}

TEST(BitMatrix, RowAccessMutatesSingleRow) {
    hbrick::BitMatrix matrix(2U, 5U);

    matrix.row(1U).set(3U);
    EXPECT_TRUE(matrix.test(1U, 3U));
    EXPECT_FALSE(matrix.test(0U, 3U));
}
