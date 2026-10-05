#include <gtest/gtest.h>

#include "hbrick/bit/bit_vector.hpp"

TEST(BitVector, SetTestResetAndClear) {
    hbrick::BitVector bits(130U);

    EXPECT_EQ(bits.numBits(), 130U);
    EXPECT_EQ(bits.numWords(), 3U);

    EXPECT_FALSE(bits.test(64U));
    bits.set(64U);
    EXPECT_TRUE(bits.test(64U));

    bits.reset(64U);
    EXPECT_FALSE(bits.test(64U));

    bits.set(129U);
    EXPECT_TRUE(bits.test(129U));

    bits.clear();
    EXPECT_FALSE(bits.test(129U));
}

TEST(BitVector, RowOrCombinesWordStorage) {
    hbrick::BitVector lhs(70U);
    hbrick::BitVector rhs(70U);

    lhs.set(0U);
    lhs.set(63U);
    rhs.set(1U);
    rhs.set(64U);

    lhs.rowOr(rhs);

    EXPECT_TRUE(lhs.test(0U));
    EXPECT_TRUE(lhs.test(1U));
    EXPECT_TRUE(lhs.test(63U));
    EXPECT_TRUE(lhs.test(64U));
    EXPECT_EQ(lhs.word(0U), (1ULL << 0U) | (1ULL << 1U) | (1ULL << 63U));
    EXPECT_EQ(lhs.word(1U), 1ULL << 0U);
}

TEST(BitVector, RowOrNoOpWhenDimensionsMismatch) {
    hbrick::BitVector lhs(64U);
    hbrick::BitVector rhs(128U);

    lhs.set(0U);
    rhs.set(100U);

    lhs.rowOr(rhs);

    EXPECT_TRUE(lhs.test(0U));
    EXPECT_FALSE(lhs.test(100U));
}

TEST(BitVector, OutOfRangeAccessIsSafe) {
    hbrick::BitVector bits(4U);

    EXPECT_FALSE(bits.test(99U));
    bits.set(99U);
    bits.reset(99U);
    EXPECT_EQ(bits.word(99U), 0U);
}

TEST(BitVector, AnyAndIntersects) {
    hbrick::BitVector empty(128U);
    hbrick::BitVector lhs(128U);
    hbrick::BitVector rhs(64U);

    EXPECT_FALSE(empty.any());
    lhs.set(70U);
    EXPECT_TRUE(lhs.any());
    EXPECT_FALSE(lhs.intersects(rhs));

    rhs.set(3U);
    EXPECT_FALSE(lhs.intersects(rhs));
    lhs.set(3U);
    EXPECT_TRUE(lhs.intersects(rhs));
}

TEST(BitVector, RowOrFromAllowsWiderScratch) {
    hbrick::BitVector scratch(256U);
    hbrick::BitVector narrow(64U);
    narrow.set(5U);
    narrow.set(63U);

    scratch.set(200U);
    scratch.rowOrFrom(narrow);

    EXPECT_TRUE(scratch.test(5U));
    EXPECT_TRUE(scratch.test(63U));
    EXPECT_TRUE(scratch.test(200U));
}

TEST(BitVector, RowOrFromDoesNotPolluteTrailingBits) {
    hbrick::BitVector target(10U);
    hbrick::BitVector wider(64U);
    wider.set(20U);

    target.rowOrFrom(wider);

    EXPECT_FALSE(target.test(20U));
    EXPECT_FALSE(target.any());
    EXPECT_EQ(target.popcount(), 0U);

    hbrick::BitVector other(64U);
    other.set(20U);
    EXPECT_FALSE(target.intersects(other));
}

