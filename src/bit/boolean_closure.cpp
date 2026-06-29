#include "hbrick/bit/boolean_closure.hpp"

#include <bit>
#include <thread>
#include <utility>
#include <vector>

namespace hbrick {

namespace {

constexpr uint32_t kMinRowsForParallelKleene = 64U;

[[nodiscard]] bool rowsDiffer(
    const BitVector& lhs,
    const BitVector& rhs
) noexcept {
    const size_t num_words = lhs.numWords();
    for (size_t word_index = 0U; word_index < num_words; ++word_index) {
        if (lhs.word(word_index) != rhs.word(word_index)) {
            return true;
        }
    }
    return false;
}

void accumulateBooleanProductRow(
    const BitVector& lhs_row,
    const BitMatrix& rhs,
    BitVector& out_row,
    const uint32_t inner_dim
) {
    out_row.clear();

    const size_t num_words = lhs_row.numWords();
    if (num_words == 0U) {
        return;
    }

    const size_t full_words = static_cast<size_t>(inner_dim) / 64U;
    const uint32_t tail_bits = inner_dim % 64U;
    const uint64_t tail_mask =
        tail_bits == 0U ? ~0ULL : ((1ULL << tail_bits) - 1ULL);

    for (size_t word_index = 0U; word_index < num_words; ++word_index) {
        uint64_t bits = lhs_row.word(word_index);
        if (word_index == full_words && tail_bits != 0U) {
            bits &= tail_mask;
        }
        while (bits != 0U) {
            const uint32_t inner = static_cast<uint32_t>(
                word_index * 64U + static_cast<size_t>(std::countr_zero(bits))
            );
            out_row.rowOr(rhs.row(inner));
            bits &= bits - 1U;
        }
    }
}

[[nodiscard]] bool booleanMultiplyInto(
    const BitMatrix& lhs,
    const BitMatrix& rhs,
    BitMatrix& out,
    const KleeneSquaringOptions& options
) noexcept {
    const uint32_t num_rows = lhs.numRows();
    const uint32_t inner_dim = lhs.numCols();
    const uint32_t thread_count = resolveKleeneThreadCount(options);
    if (!options.use_parallel
        || thread_count <= 1U
        || num_rows < kMinRowsForParallelKleene) {
        bool changed = false;
        for (uint32_t row = 0U; row < num_rows; ++row) {
            accumulateBooleanProductRow(lhs.row(row), rhs, out.row(row), inner_dim);
            if (rowsDiffer(out.row(row), lhs.row(row))) {
                changed = true;
            }
        }
        return changed;
    }

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    std::vector<uint8_t> changed_flags(thread_count, 0U);

    const uint32_t rows_per_thread =
        (num_rows + thread_count - 1U) / thread_count;
    for (uint32_t worker_index = 0U; worker_index < thread_count; ++worker_index) {
        const uint32_t row_begin = worker_index * rows_per_thread;
        if (row_begin >= num_rows) {
            break;
        }
        const uint32_t row_end = std::min(row_begin + rows_per_thread, num_rows);
        workers.emplace_back([&, row_begin, row_end, worker_index]() {
            bool local_changed = false;
            for (uint32_t row = row_begin; row < row_end; ++row) {
                accumulateBooleanProductRow(
                    lhs.row(row),
                    rhs,
                    out.row(row),
                    inner_dim
                );
                if (rowsDiffer(out.row(row), lhs.row(row))) {
                    local_changed = true;
                }
            }
            if (local_changed) {
                changed_flags[worker_index] = 1U;
            }
        });
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    for (const uint8_t flag : changed_flags) {
        if (flag != 0U) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool booleanMultiplyInto(
    const BitMatrix& lhs,
    const BitMatrix& rhs,
    BitMatrix& out
) noexcept {
    return booleanMultiplyInto(lhs, rhs, out, KleeneSquaringOptions{});
}

[[nodiscard]] BitMatrix& ensureScratch(
    BitMatrix& owned_scratch,
    BitMatrix* external_scratch,
    const uint32_t num_vertices
) {
    if (external_scratch != nullptr) {
        if (external_scratch->numRows() != num_vertices
            || external_scratch->numCols() != num_vertices) {
            *external_scratch = BitMatrix(num_vertices, num_vertices);
        }
        return *external_scratch;
    }

    if (owned_scratch.numRows() != num_vertices || owned_scratch.numCols() != num_vertices) {
        owned_scratch = BitMatrix(num_vertices, num_vertices);
    }
    return owned_scratch;
}

}  // namespace

BitMatrix booleanMultiply(const BitMatrix& lhs, const BitMatrix& rhs) {
    if (lhs.numCols() != rhs.numRows()) {
        return BitMatrix{};
    }

    BitMatrix product(lhs.numRows(), rhs.numCols());
    (void)booleanMultiplyInto(lhs, rhs, product);
    return product;
}

bool bitMatricesEqual(const BitMatrix& lhs, const BitMatrix& rhs) noexcept {
    if (lhs.numRows() != rhs.numRows() || lhs.numCols() != rhs.numCols()) {
        return false;
    }

    for (uint32_t row = 0U; row < lhs.numRows(); ++row) {
        if (rowsDiffer(lhs.row(row), rhs.row(row))) {
            return false;
        }
    }
    return true;
}

uint32_t BooleanClosure::kleeneSquaringCountForLargestComponent(
    const uint32_t largest_component_size
) noexcept {
    if (largest_component_size <= 1U) {
        return 0U;
    }

    uint32_t squaring_count = 0U;
    uint32_t covered = 1U;
    while (covered < largest_component_size) {
        covered <<= 1U;
        ++squaring_count;
    }
    return squaring_count;
}

void BooleanClosure::transitiveClosureKleeneSquaringInPlace(
    BitMatrix& relation,
    const uint32_t squaring_count,
    BitMatrix* scratch,
    const KleeneSquaringOptions options
) {
    const uint32_t num_vertices = relation.numRows();
    if (num_vertices != relation.numCols() || squaring_count == 0U) {
        return;
    }

    BitMatrix owned_scratch{};
    BitMatrix& scratch_matrix = ensureScratch(owned_scratch, scratch, num_vertices);

    for (uint32_t step = 0U; step < squaring_count; ++step) {
        if (transitiveClosureKleeneSquaringStepInPlace(relation, scratch_matrix, options)) {
            return;
        }
    }
}

bool BooleanClosure::transitiveClosureKleeneSquaringStepInPlace(
    BitMatrix& relation,
    BitMatrix& scratch,
    const KleeneSquaringOptions options
) noexcept {
    const uint32_t num_vertices = relation.numRows();
    if (num_vertices != relation.numCols()) {
        return true;
    }

    BitMatrix owned_scratch{};
    BitMatrix& scratch_matrix = ensureScratch(owned_scratch, &scratch, num_vertices);
    if (!booleanMultiplyInto(relation, relation, scratch_matrix, options)) {
        return true;
    }
    std::swap(relation, scratch_matrix);
    return false;
}

void BooleanClosure::transitiveClosureWarshallInPlace(BitMatrix& relation) {
    const uint32_t num_vertices = relation.numRows();
    if (num_vertices != relation.numCols()) {
        return;
    }

    for (uint32_t pivot = 0U; pivot < num_vertices; ++pivot) {
        const BitVector& pivot_row = relation.row(pivot);
        const size_t pivot_word_index = static_cast<size_t>(pivot) / 64U;
        const uint64_t pivot_mask = 1ULL << (static_cast<size_t>(pivot) % 64U);

        for (uint32_t row_index = 0U; row_index < num_vertices; ++row_index) {
            if (row_index == pivot) {
                continue;
            }
            if ((relation.row(row_index).word(pivot_word_index) & pivot_mask) != 0U) {
                relation.row(row_index).rowOr(pivot_row);
            }
        }
    }
}

BitMatrix BooleanClosure::transitiveClosureWarshall(BitMatrix relation) {
    transitiveClosureWarshallInPlace(relation);
    return relation;
}

}  // namespace hbrick
