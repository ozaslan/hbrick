#include "hbrick/bit/bit_vector.hpp"

#include <algorithm>
#include <bit>
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

bool BitVector::any() const noexcept {
    for (const uint64_t word : words_) {
        if (word != 0U) {
            return true;
        }
    }
    return false;
}

size_t BitVector::popcount() const noexcept {
    size_t count = 0U;
    for (const uint64_t word : words_) {
        count += static_cast<size_t>(std::popcount(word));
    }
    return count;
}

bool BitVector::intersects(const BitVector& other) const noexcept {
    const size_t word_limit = std::min(words_.size(), other.words_.size());
    const uint64_t* const a = words_.data();
    const uint64_t* const b = other.words_.data();
    for (size_t word_index = 0; word_index < word_limit; ++word_index) {
        if ((a[word_index] & b[word_index]) != 0U) {
            return true;
        }
    }
    return false;
}

void BitVector::rowOr(const BitVector& other) noexcept {
    assert(num_bits_ == other.num_bits_);
    if (num_bits_ != other.num_bits_ || this == &other) {
        return;
    }

    uint64_t* const dst = words_.data();
    const uint64_t* const src = other.words_.data();
    const size_t count = words_.size();
    for (size_t word_index = 0; word_index < count; ++word_index) {
        dst[word_index] |= src[word_index];
    }
}

void BitVector::rowOrFrom(const BitVector& other) noexcept {
    if (this == &other) {
        return;
    }
    const size_t word_limit = std::min(words_.size(), other.words_.size());
    uint64_t* const dst = words_.data();
    const uint64_t* const src = other.words_.data();
    for (size_t word_index = 0; word_index < word_limit; ++word_index) {
        dst[word_index] |= src[word_index];
    }
    if (num_bits_ % 64U != 0U && !words_.empty()) {
        const uint64_t tail_mask = (1ULL << (num_bits_ % 64U)) - 1ULL;
        words_.back() &= tail_mask;
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
