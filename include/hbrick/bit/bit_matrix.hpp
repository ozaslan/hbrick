/**
 * @file bit_matrix.hpp
 * @ingroup hbrick_bit
 * @brief Row-major boolean matrix built from @ref hbrick::BitVector rows.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "hbrick/bit/bit_vector.hpp"

namespace hbrick {

/**
 * @brief Dense boolean matrix with one @ref hbrick::BitVector per row.
 * @ingroup hbrick_bit
 *
 * Used to represent adjacency relations and their transitive closures.
 * Row access enables bit-parallel Warshall updates in @ref hbrick::BooleanClosure.
 */
class BitMatrix {
public:
    /** @brief Constructs an empty matrix with zero rows and columns. @ingroup hbrick_bit */
    BitMatrix() = default;

    /**
     * @brief Constructs a @p num_rows by @p num_cols matrix, all bits cleared.
     * @ingroup hbrick_bit
     *
     * @param num_rows Row count (source vertices in adjacency use cases).
     * @param num_cols Column count (target vertices in adjacency use cases).
     */
    BitMatrix(uint32_t num_rows, uint32_t num_cols);

    /** @brief Returns the number of matrix rows. @return The number of matrix rows. @ingroup hbrick_bit */
    [[nodiscard]] uint32_t numRows() const noexcept { return num_rows_; }
    /** @brief Returns the number of matrix columns. @return The number of matrix columns. @ingroup hbrick_bit */
    [[nodiscard]] uint32_t numCols() const noexcept { return num_cols_; }

    /**
     * @brief Tests the bit at (@p row, @p col).
     * @ingroup hbrick_bit
     *
     * @param row Row index.
     * @param col Column index.
     * @return @c true when the bit is set.
     */
    [[nodiscard]] bool test(uint32_t row, uint32_t col) const noexcept;

    /**
     * @brief Sets the bit at (@p row, @p col) to one.
     * @ingroup hbrick_bit
     *
     * @param row Row index.
     * @param col Column index.
     */
    void set(uint32_t row, uint32_t col) noexcept;

    /**
     * @brief Clears the bit at (@p row, @p col) to zero.
     * @ingroup hbrick_bit
     *
     * @param row Row index.
     * @param col Column index.
     */
    void reset(uint32_t row, uint32_t col) noexcept;

    /**
     * @brief Returns mutable access to row @p row_index.
     * @ingroup hbrick_bit
     *
     * @param row_index Zero-based row index.
     * @return Mutable reference to the row bit vector.
     */
    [[nodiscard]] BitVector& row(uint32_t row_index) noexcept;

    /**
     * @brief Returns read-only access to row @p row_index.
     * @ingroup hbrick_bit
     *
     * @param row_index Zero-based row index.
     * @return Read-only reference to the row bit vector.
     */
    [[nodiscard]] const BitVector& row(uint32_t row_index) const noexcept;

    /**
     * @brief Estimates total heap memory used by all row storage.
     * @ingroup hbrick_bit
     *
     * @return Approximate byte count including vector capacity overhead.
     */
    [[nodiscard]] uint64_t memoryBytes() const noexcept;

private:
    uint32_t num_rows_ = 0;
    uint32_t num_cols_ = 0;
    std::vector<BitVector> rows_;
};

}  // namespace hbrick
