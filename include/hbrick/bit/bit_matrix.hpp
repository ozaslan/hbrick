#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/bit/bit_vector.hpp"

namespace hbrick {

class BitMatrix {
public:
    BitMatrix() = default;
    BitMatrix(uint32_t num_rows, uint32_t num_cols);

    [[nodiscard]] uint32_t numRows() const noexcept { return num_rows_; }
    [[nodiscard]] uint32_t numCols() const noexcept { return num_cols_; }

    [[nodiscard]] bool test(uint32_t row, uint32_t col) const noexcept;
    void set(uint32_t row, uint32_t col) noexcept;
    void reset(uint32_t row, uint32_t col) noexcept;

    [[nodiscard]] BitVector& row(uint32_t row_index) noexcept;
    [[nodiscard]] const BitVector& row(uint32_t row_index) const noexcept;

    [[nodiscard]] uint64_t memoryBytes() const noexcept;

private:
    uint32_t num_rows_ = 0;
    uint32_t num_cols_ = 0;
    std::vector<BitVector> rows_;
};

}  // namespace hbrick
