/**
 * @file bit_vector.hpp
 * @ingroup hbrick_bit
 * @brief Fixed-width bitset stored as 64-bit words.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hbrick {

/**
 * @brief Compact bit vector backed by a contiguous array of @c uint64_t words.
 * @ingroup hbrick_bit
 *
 * Used as rows of @ref hbrick::BitMatrix and in bit-parallel graph algorithms.
 * Hot-path operations (@ref test, @ref set, @ref rowOr) perform no heap
 * allocation.
 */
class BitVector {
public:
    /** @brief Constructs an empty bit vector with zero bits. @ingroup hbrick_bit */
    BitVector() = default;

    /**
     * @brief Constructs a bit vector with @p num_bits bits, all cleared.
     * @ingroup hbrick_bit
     *
     * @param num_bits Logical bit width. May be zero.
     */
    explicit BitVector(size_t num_bits);

    /** @brief Returns the logical number of bits in the vector. @return The logical number of bits in the vector. @ingroup hbrick_bit */
    [[nodiscard]] size_t numBits() const noexcept { return num_bits_; }
    /** @brief Returns the number of underlying 64-bit storage words. @return The number of underlying 64-bit storage words. @ingroup hbrick_bit */
    [[nodiscard]] size_t numWords() const noexcept { return words_.size(); }

    /**
     * @brief Computes how many 64-bit words are required for @p num_bits.
     * @ingroup hbrick_bit
     *
     * @param num_bits Logical bit width.
     * @return Word count using ceiling division by 64.
     */
    [[nodiscard]] static size_t wordCount(size_t num_bits) noexcept;

    /**
     * @brief Tests whether bit @p bit_index is set.
     * @ingroup hbrick_bit
     *
     * @param bit_index Zero-based bit position. Must be less than @ref numBits().
     * @return @c true when the bit is set.
     */
    [[nodiscard]] bool test(size_t bit_index) const noexcept;

    /**
     * @brief Sets bit @p bit_index to one.
     * @ingroup hbrick_bit
     *
     * @param bit_index Zero-based bit position. Must be less than @ref numBits().
     */
    void set(size_t bit_index) noexcept;

    /**
     * @brief Clears bit @p bit_index to zero.
     * @ingroup hbrick_bit
     *
     * @param bit_index Zero-based bit position. Must be less than @ref numBits().
     */
    void reset(size_t bit_index) noexcept;

    /** @brief Clears every bit in the vector to zero. @ingroup hbrick_bit */
    void clear() noexcept;

    /**
     * @brief Bitwise ORs @p other into this vector in place.
     * @ingroup hbrick_bit
     *
     * Both vectors must have the same @ref numBits().
     *
     * @param other Source vector OR-ed into this one.
     */
    void rowOr(const BitVector& other) noexcept;

    /**
     * @brief Returns raw storage word @p word_index.
     * @ingroup hbrick_bit
     *
     * @param word_index Index into the underlying word array.
     * @return Documented value.
     */
    [[nodiscard]] uint64_t word(size_t word_index) const noexcept;

    /** @brief Returns a pointer to the read-only underlying word storage. @return A pointer to the read-only underlying word storage. @ingroup hbrick_bit */
    [[nodiscard]] const uint64_t* wordsData() const noexcept;
    /** @brief Returns a pointer to the mutable underlying word storage. @return A pointer to the mutable underlying word storage. @ingroup hbrick_bit */
    [[nodiscard]] uint64_t* wordsData() noexcept;

private:
    size_t num_bits_ = 0;
    std::vector<uint64_t> words_;
};

}  // namespace hbrick
