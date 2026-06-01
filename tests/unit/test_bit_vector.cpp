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

TEST(BitVector, OutOfRangeAccessIsSafe) {
    hbrick::BitVector bits(4U);

    EXPECT_FALSE(bits.test(99U));
    bits.set(99U);
    bits.reset(99U);
    EXPECT_EQ(bits.word(99U), 0U);
}
