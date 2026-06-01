#include "hbrick/bit/bit_matrix.hpp"

namespace hbrick {

BitMatrix::BitMatrix(const uint32_t num_rows, const uint32_t num_cols)
    : num_rows_(num_rows), num_cols_(num_cols), rows_(num_rows, BitVector(num_cols)) {}

bool BitMatrix::test(const uint32_t row, const uint32_t col) const noexcept {
    if (row >= num_rows_) {
        return false;
    }
    return rows_[row].test(col);
}

void BitMatrix::set(const uint32_t row, const uint32_t col) noexcept {
    if (row >= num_rows_) {
        return;
    }
    rows_[row].set(col);
}

void BitMatrix::reset(const uint32_t row, const uint32_t col) noexcept {
    if (row >= num_rows_) {
        return;
    }
    rows_[row].reset(col);
}

BitVector& BitMatrix::row(const uint32_t row_index) noexcept {
    return rows_[row_index];
}

const BitVector& BitMatrix::row(const uint32_t row_index) const noexcept {
    return rows_[row_index];
}

uint64_t BitMatrix::memoryBytes() const noexcept {
    uint64_t total = 0;
    for (const BitVector& row : rows_) {
        total += static_cast<uint64_t>(row.numWords()) * sizeof(uint64_t);
    }
    return total;
}

}  // namespace hbrick
