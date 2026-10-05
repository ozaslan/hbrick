#include <gtest/gtest.h>

#include "hbrick/bit/bit_matrix.hpp"

TEST(BitMatrix, StoresNonMultipleOf64Columns) {
    hbrick::BitMatrix matrix(3U, 70U);

    EXPECT_EQ(matrix.numRows(), 3U);
    EXPECT_EQ(matrix.numCols(), 70U);
    EXPECT_EQ(matrix.row(0U).numWords(), 2U);
    EXPECT_EQ(matrix.memoryBytes(), 3U * sizeof(hbrick::BitVector) + 3U * 2U * sizeof(uint64_t));

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

TEST(BitMatrix, BoundaryRowAndColumnAccess) {
    // 1×1 edge case: single row, single column.
    hbrick::BitMatrix matrix(1U, 1U);
    EXPECT_EQ(matrix.numRows(), 1U);
    EXPECT_EQ(matrix.numCols(), 1U);
    EXPECT_FALSE(matrix.test(0U, 0U));
    matrix.set(0U, 0U);
    EXPECT_TRUE(matrix.test(0U, 0U));
    matrix.reset(0U, 0U);
    EXPECT_FALSE(matrix.test(0U, 0U));

    // Last row / last column of a non-trivial matrix.
    hbrick::BitMatrix m2(4U, 100U);
    m2.set(3U, 99U);
    EXPECT_TRUE(m2.test(3U, 99U));
    EXPECT_FALSE(m2.test(3U, 98U));
    EXPECT_FALSE(m2.test(2U, 99U));
}

TEST(BitMatrix, LargeMatrixRoundTrips) {
    // 256×512: crosses multiple word boundaries.
    constexpr uint32_t kRows = 256U;
    constexpr uint32_t kCols = 512U;
    hbrick::BitMatrix matrix(kRows, kCols);
    EXPECT_EQ(matrix.numRows(), kRows);
    EXPECT_EQ(matrix.numCols(), kCols);

    // Set a checkerboard pattern and verify.
    for (uint32_t r = 0U; r < kRows; ++r) {
        for (uint32_t c = 0U; c < kCols; c += 64U) {
            if ((r + c) % 128U == 0U) {
                matrix.set(r, c);
            }
        }
    }
    for (uint32_t r = 0U; r < kRows; ++r) {
        for (uint32_t c = 0U; c < kCols; c += 64U) {
            EXPECT_EQ(matrix.test(r, c), (r + c) % 128U == 0U);
        }
    }
}

TEST(BitMatrix, MemoryBytesConsistentWithNumRowsCols) {
    // memoryBytes() returns rows vector capacity * sizeof(BitVector) + rows * wordsPerRow * sizeof(uint64_t).
    constexpr uint32_t kRows = 7U;
    constexpr uint32_t kCols = 200U;
    hbrick::BitMatrix matrix(kRows, kCols);
    const size_t expect_words = hbrick::BitVector::wordCount(kCols);
    EXPECT_EQ(
        matrix.memoryBytes(),
        kRows * sizeof(hbrick::BitVector) + kRows * expect_words * sizeof(uint64_t)
    );
}
