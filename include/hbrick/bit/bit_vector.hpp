#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hbrick {

class BitVector {
public:
    BitVector() = default;
    explicit BitVector(size_t num_bits);

    [[nodiscard]] size_t numBits() const noexcept { return num_bits_; }
    [[nodiscard]] size_t numWords() const noexcept { return words_.size(); }

    [[nodiscard]] static size_t wordCount(size_t num_bits) noexcept;

    [[nodiscard]] bool test(size_t bit_index) const noexcept;
    void set(size_t bit_index) noexcept;
    void reset(size_t bit_index) noexcept;
    void clear() noexcept;

    void rowOr(const BitVector& other) noexcept;

    [[nodiscard]] uint64_t word(size_t word_index) const noexcept;
    [[nodiscard]] const uint64_t* wordsData() const noexcept;
    [[nodiscard]] uint64_t* wordsData() noexcept;

private:
    size_t num_bits_ = 0;
    std::vector<uint64_t> words_;
};

}  // namespace hbrick
