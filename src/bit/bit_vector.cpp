#include "hbrick/bit/bit_vector.hpp"

#include <algorithm>
#include <cassert>

namespace hbrick {

namespace {

constexpr size_t kBitsPerWord = 64U;

}  // namespace

BitVector::BitVector(const size_t num_bits) : num_bits_(num_bits), words_(wordCount(num_bits), 0U) {}

size_t BitVector::wordCount(const size_t num_bits) noexcept {
    return (num_bits + (kBitsPerWord - 1U)) / kBitsPerWord;
}

bool BitVector::test(const size_t bit_index) const noexcept {
    if (bit_index >= num_bits_) {
        return false;
    }

    const size_t word_index = bit_index / kBitsPerWord;
    const uint64_t mask = 1ULL << (bit_index % kBitsPerWord);
    return (words_[word_index] & mask) != 0U;
}

void BitVector::set(const size_t bit_index) noexcept {
    if (bit_index >= num_bits_) {
        return;
    }

    const size_t word_index = bit_index / kBitsPerWord;
    const uint64_t mask = 1ULL << (bit_index % kBitsPerWord);
    words_[word_index] |= mask;
}

void BitVector::reset(const size_t bit_index) noexcept {
    if (bit_index >= num_bits_) {
        return;
    }

    const size_t word_index = bit_index / kBitsPerWord;
    const uint64_t mask = 1ULL << (bit_index % kBitsPerWord);
    words_[word_index] &= ~mask;
}

void BitVector::clear() noexcept {
    std::fill(words_.begin(), words_.end(), 0U);
}

void BitVector::rowOr(const BitVector& other) noexcept {
    assert(num_bits_ == other.num_bits_);
    if (num_bits_ != other.num_bits_) {
        return;
    }

    for (size_t word_index = 0; word_index < words_.size(); ++word_index) {
        words_[word_index] |= other.words_[word_index];
    }
}

uint64_t BitVector::word(const size_t word_index) const noexcept {
    if (word_index >= words_.size()) {
        return 0U;
    }
    return words_[word_index];
}

const uint64_t* BitVector::wordsData() const noexcept {
    return words_.data();
}

uint64_t* BitVector::wordsData() noexcept {
    return words_.data();
}

}  // namespace hbrick
