#include <gtest/gtest.h>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/bit/boolean_closure.hpp"

namespace {

void setReflexive(hbrick::BitMatrix& matrix) {
    const uint32_t n = matrix.numRows();
    for (uint32_t vertex = 0; vertex < n; ++vertex) {
        matrix.set(vertex, vertex);
    }
}

}  // namespace

TEST(BooleanClosure, ComputesManualThreeVertexClosure) {
    hbrick::BitMatrix relation(3U, 3U);
    setReflexive(relation);
    relation.set(0U, 1U);
    relation.set(1U, 2U);

    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(relation);

    EXPECT_TRUE(relation.test(0U, 2U));
    EXPECT_FALSE(relation.test(2U, 0U));
    EXPECT_TRUE(relation.test(1U, 2U));
}

TEST(BooleanClosure, CopyingOverloadMatchesInPlaceResult) {
    hbrick::BitMatrix in_place(4U, 4U);
    hbrick::BitMatrix copied(4U, 4U);
    setReflexive(in_place);
    setReflexive(copied);
    in_place.set(0U, 1U);
    in_place.set(1U, 2U);
    in_place.set(2U, 3U);
    copied.set(0U, 1U);
    copied.set(1U, 2U);
    copied.set(2U, 3U);

    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(in_place);
    const hbrick::BitMatrix closed = hbrick::BooleanClosure::transitiveClosureWarshall(copied);

    for (uint32_t row = 0; row < 4U; ++row) {
        for (uint32_t col = 0; col < 4U; ++col) {
            EXPECT_EQ(in_place.test(row, col), closed.test(row, col)) << row << "," << col;
        }
    }
}

TEST(BooleanClosure, NonSquareMatrixIsLeftUnchanged) {
    hbrick::BitMatrix relation(3U, 5U);
    relation.set(0U, 1U);

    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(relation);

    EXPECT_TRUE(relation.test(0U, 1U));
    EXPECT_FALSE(relation.test(0U, 4U));
}

TEST(BooleanClosure, ZeroSizeMatrixIsNoOp) {
    hbrick::BitMatrix relation(0U, 0U);

    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(relation);
    const hbrick::BitMatrix closed = hbrick::BooleanClosure::transitiveClosureWarshall(relation);

    EXPECT_EQ(closed.numRows(), 0U);
    EXPECT_EQ(closed.numCols(), 0U);
}

TEST(BooleanClosure, WorksWithNonMultipleOf64Columns) {
    hbrick::BitMatrix relation(70U, 70U);
    setReflexive(relation);
    relation.set(0U, 69U);
    relation.set(69U, 68U);

    hbrick::BooleanClosure::transitiveClosureWarshallInPlace(relation);

    EXPECT_TRUE(relation.test(0U, 68U));
    EXPECT_FALSE(relation.test(68U, 0U));
}
