#include "hbrick/bit/boolean_closure.hpp"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <bit>
#include <cstdint>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

namespace hbrick {

namespace {

constexpr uint32_t kMinRowsForParallelKleene = 1024U;

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

void buildM4RMTable(
    BitVector* table,
    const BitMatrix& rhs,
    const uint32_t p,
    const uint32_t k
) {
    table[0].clear();
    for (uint32_t b = 0U; b < k; ++b) {
        const BitVector& r = rhs.row(p + b);
        const uint32_t half = 1U << b;
        for (uint32_t idx = 0U; idx < half; ++idx) {
            table[half | idx] = table[idx];
            table[half | idx].rowOr(r);
        }
    }
}

void multiplyM4RM4TableRows(
    const BitMatrix& lhs,
    BitMatrix& out,
    const std::vector<std::vector<BitVector>>& tables,
    const size_t stripe,
    const size_t active,
    const uint32_t r_begin,
    const uint32_t r_end
) {
    if (active == 4U) {
        for (uint32_t row = r_begin; row < r_end; ++row) {
            const uint8_t* const row_bytes = reinterpret_cast<const uint8_t*>(lhs.row(row).wordsData());
            uint32_t raw_code = 0U;
            std::memcpy(&raw_code, &row_bytes[stripe], sizeof(uint32_t));
            if (raw_code == 0U) {
                continue;
            }
            BitVector& out_r = out.row(row);
            const uint8_t c0 = static_cast<uint8_t>(raw_code & 0xFF);
            const uint8_t c1 = static_cast<uint8_t>((raw_code >> 8) & 0xFF);
            const uint8_t c2 = static_cast<uint8_t>((raw_code >> 16) & 0xFF);
            const uint8_t c3 = static_cast<uint8_t>((raw_code >> 24) & 0xFF);
            if (c0 != 0U) out_r.rowOr(tables[0][c0]);
            if (c1 != 0U) out_r.rowOr(tables[1][c1]);
            if (c2 != 0U) out_r.rowOr(tables[2][c2]);
            if (c3 != 0U) out_r.rowOr(tables[3][c3]);
        }
    } else {
        for (uint32_t row = r_begin; row < r_end; ++row) {
            const uint8_t* const row_bytes = reinterpret_cast<const uint8_t*>(lhs.row(row).wordsData());
            BitVector& out_r = out.row(row);
            for (size_t t = 0U; t < active; ++t) {
                const uint8_t code = row_bytes[stripe + t];
                if (code != 0U) {
                    out_r.rowOr(tables[t][code]);
                }
            }
        }
    }
}

void multiplyM4RM8TableRows(
    const BitMatrix& lhs,
    BitMatrix& out,
    const std::vector<std::vector<BitVector>>& tables,
    const size_t word_idx,
    const size_t active,
    const uint32_t r_begin,
    const uint32_t r_end
) {
    if (active == 8U) {
        for (uint32_t row = r_begin; row < r_end; ++row) {
            const uint64_t w = lhs.row(row).word(word_idx);
            if (w == 0ULL) {
                continue;
            }
            BitVector& out_r = out.row(row);
            if (w & 0xFF) out_r.rowOr(tables[0][static_cast<uint8_t>(w & 0xFF)]);
            if ((w >> 8) & 0xFF) out_r.rowOr(tables[1][static_cast<uint8_t>((w >> 8) & 0xFF)]);
            if ((w >> 16) & 0xFF) out_r.rowOr(tables[2][static_cast<uint8_t>((w >> 16) & 0xFF)]);
            if ((w >> 24) & 0xFF) out_r.rowOr(tables[3][static_cast<uint8_t>((w >> 24) & 0xFF)]);
            if ((w >> 32) & 0xFF) out_r.rowOr(tables[4][static_cast<uint8_t>((w >> 32) & 0xFF)]);
            if ((w >> 40) & 0xFF) out_r.rowOr(tables[5][static_cast<uint8_t>((w >> 40) & 0xFF)]);
            if ((w >> 48) & 0xFF) out_r.rowOr(tables[6][static_cast<uint8_t>((w >> 48) & 0xFF)]);
            if ((w >> 56) & 0xFF) out_r.rowOr(tables[7][static_cast<uint8_t>((w >> 56) & 0xFF)]);
        }
    } else {
        for (uint32_t row = r_begin; row < r_end; ++row) {
            const uint64_t w = lhs.row(row).word(word_idx);
            if (w == 0ULL) {
                continue;
            }
            BitVector& out_r = out.row(row);
            for (size_t t = 0U; t < active; ++t) {
                const uint8_t code = static_cast<uint8_t>((w >> (t * 8U)) & 0xFF);
                if (code != 0U) {
                    out_r.rowOr(tables[t][code]);
                }
            }
        }
    }
}

void multiplyM4RMSerial(
    const BitMatrix& lhs,
    const BitMatrix& rhs,
    BitMatrix& out,
    std::vector<std::vector<BitVector>>& tables,
    const uint32_t num_tables
) {
    const uint32_t n = lhs.numRows();
    out = lhs;
    const size_t num_stripes = (static_cast<size_t>(n) + 7U) / 8U;

    if (num_tables == 8U) {
        const size_t num_words = (static_cast<size_t>(n) + 63U) / 64U;
        for (size_t word_idx = 0U; word_idx < num_words; ++word_idx) {
            const size_t stripe_base = word_idx * 8U;
            const size_t active = std::min<size_t>(8U, num_stripes - stripe_base);
            for (size_t t = 0U; t < active; ++t) {
                const uint32_t p = static_cast<uint32_t>((stripe_base + t) * 8U);
                const uint32_t k = std::min(8U, n - p);
                buildM4RMTable(tables[t].data(), rhs, p, k);
            }
            multiplyM4RM8TableRows(lhs, out, tables, word_idx, active, 0U, n);
        }
    } else {
        for (size_t stripe = 0U; stripe < num_stripes; stripe += 4U) {
            const size_t active = std::min<size_t>(4U, num_stripes - stripe);
            for (size_t t = 0U; t < active; ++t) {
                const uint32_t p = static_cast<uint32_t>((stripe + t) * 8U);
                const uint32_t k = std::min(8U, n - p);
                buildM4RMTable(tables[t].data(), rhs, p, k);
            }
            multiplyM4RM4TableRows(lhs, out, tables, stripe, active, 0U, n);
        }
    }
}

void runParallelM4RMSquaring(
    BitMatrix& relation,
    BitMatrix& scratch_matrix,
    const uint32_t squaring_count,
    const uint32_t num_tables,
    const uint32_t thread_count,
    uint32_t* actual_rounds,
    bool* reached_fixpoint
) {
    const uint32_t n = relation.numRows();
    const size_t num_stripes = (static_cast<size_t>(n) + 7U) / 8U;
    const size_t num_words = (static_cast<size_t>(n) + 63U) / 64U;

    std::vector<std::vector<BitVector>> tables(num_tables, std::vector<BitVector>(256, BitVector(n)));
    const uint32_t rows_per_worker = (n + thread_count - 1U) / thread_count;
    std::barrier sync_point(thread_count);
    std::atomic<bool> global_stop{false};
    uint32_t completed_steps = 0U;

    auto worker_fn = [&](const uint32_t tid) {
        const uint32_t r_begin = tid * rows_per_worker;
        const uint32_t r_end = std::min(r_begin + rows_per_worker, n);

        for (uint32_t step = 0U; step < squaring_count; ++step) {
            if (r_begin < r_end) {
                for (uint32_t r = r_begin; r < r_end; ++r) {
                    scratch_matrix.row(r) = relation.row(r);
                }
            }
            sync_point.arrive_and_wait();

            if (num_tables == 8U) {
                for (size_t word_idx = 0U; word_idx < num_words; ++word_idx) {
                    const size_t stripe_base = word_idx * 8U;
                    const size_t active = std::min<size_t>(8U, num_stripes - stripe_base);

                    for (size_t t = tid; t < active; t += thread_count) {
                        const uint32_t p = static_cast<uint32_t>((stripe_base + t) * 8U);
                        const uint32_t k = std::min(8U, n - p);
                        buildM4RMTable(tables[t].data(), relation, p, k);
                    }
                    sync_point.arrive_and_wait();

                    if (r_begin < r_end) {
                        multiplyM4RM8TableRows(relation, scratch_matrix, tables, word_idx, active, r_begin, r_end);
                    }
                    sync_point.arrive_and_wait();
                }
            } else {
                for (size_t stripe = 0U; stripe < num_stripes; stripe += 4U) {
                    const size_t active = std::min<size_t>(4U, num_stripes - stripe);

                    for (size_t t = tid; t < active; t += thread_count) {
                        const uint32_t p = static_cast<uint32_t>((stripe + t) * 8U);
                        const uint32_t k = std::min(8U, n - p);
                        buildM4RMTable(tables[t].data(), relation, p, k);
                    }
                    sync_point.arrive_and_wait();

                    if (r_begin < r_end) {
                        multiplyM4RM4TableRows(relation, scratch_matrix, tables, stripe, active, r_begin, r_end);
                    }
                    sync_point.arrive_and_wait();
                }
            }

            if (tid == 0U) {
                const bool identical = bitMatricesEqual(relation, scratch_matrix);
                std::swap(relation, scratch_matrix);
                completed_steps = step + 1U;
                if (identical) {
                    global_stop.store(true, std::memory_order_relaxed);
                }
            }
            sync_point.arrive_and_wait();

            if (global_stop.load(std::memory_order_relaxed)) {
                break;
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (uint32_t tid = 0U; tid < thread_count; ++tid) {
        workers.emplace_back(worker_fn, tid);
    }
    for (std::thread& w : workers) {
        w.join();
    }

    if (actual_rounds != nullptr) *actual_rounds = completed_steps;
    if (reached_fixpoint != nullptr) *reached_fixpoint = global_stop.load(std::memory_order_relaxed);
}

void accumulateBooleanProductRow(
    const BitVector& lhs_row,
    const BitMatrix& rhs,
    BitVector& out_row,
    const uint32_t inner_dim
) {
    out_row.clear();

    const size_t num_words = lhs_row.numWords();
    if (num_words == 0U || inner_dim == 0U) {
        return;
    }

    const size_t full_words = static_cast<size_t>(inner_dim) / 64U;
    const uint32_t tail_bits = inner_dim % 64U;
    const uint64_t tail_mask =
        tail_bits == 0U ? ~0ULL : ((1ULL << tail_bits) - 1ULL);
    const size_t words_to_scan = std::min(
        num_words,
        (static_cast<size_t>(inner_dim) + 63U) / 64U
    );

    for (size_t word_index = 0U; word_index < words_to_scan; ++word_index) {
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

bool booleanMultiplyInto(
    const BitMatrix& lhs,
    const BitMatrix& rhs,
    BitMatrix& out,
    const KleeneSquaringOptions& options
) {
    if (lhs.numCols() != rhs.numRows()) {
        return false;
    }

    if (&out == &lhs || &out == &rhs) {
        BitMatrix temp_out(lhs.numRows(), rhs.numCols());
        const bool changed = booleanMultiplyInto(lhs, rhs, temp_out, options);
        out = std::move(temp_out);
        return changed;
    }

    if (out.numRows() != lhs.numRows() || out.numCols() != rhs.numCols()) {
        out = BitMatrix(lhs.numRows(), rhs.numCols());
    }

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

    struct alignas(64) WorkerState {
        bool changed = false;
    };
    std::vector<WorkerState> worker_states(thread_count);

    std::vector<std::thread> workers;
    workers.reserve(thread_count);

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
                worker_states[worker_index].changed = true;
            }
        });
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    for (const WorkerState& state : worker_states) {
        if (state.changed) {
            return true;
        }
    }
    return false;
}

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
    return static_cast<uint32_t>(std::bit_width(largest_component_size - 1U));
}

void BooleanClosure::transitiveClosureM4RMSquaringInPlace(
    BitMatrix& relation,
    const uint32_t squaring_count,
    BitMatrix* scratch,
    const uint32_t table_count,
    const KleeneSquaringOptions options,
    uint32_t* executed_squarings,
    bool* reached_fixpoint
) {
    if (executed_squarings != nullptr) {
        *executed_squarings = 0U;
    }
    if (reached_fixpoint != nullptr) {
        *reached_fixpoint = false;
    }

    const uint32_t num_vertices = relation.numRows();
    if (num_vertices != relation.numCols() || squaring_count == 0U) {
        if (reached_fixpoint != nullptr) {
            *reached_fixpoint = true;
        }
        return;
    }

    BitMatrix owned_scratch{};
    BitMatrix* effective_scratch = (scratch == &relation) ? nullptr : scratch;
    BitMatrix& scratch_matrix = ensureScratch(owned_scratch, effective_scratch, num_vertices);

    const uint32_t num_tables = (table_count == 8U) ? 8U : 4U;
    const uint32_t thread_count = resolveKleeneThreadCount(options);
    const bool use_parallel = options.use_parallel && thread_count > 1U && num_vertices >= kMinRowsForParallelKleene;

    if (use_parallel) {
        runParallelM4RMSquaring(
            relation,
            scratch_matrix,
            squaring_count,
            num_tables,
            thread_count,
            executed_squarings,
            reached_fixpoint
        );
        return;
    }

    std::vector<std::vector<BitVector>> tables(num_tables, std::vector<BitVector>(256, BitVector(num_vertices)));
    uint32_t step = 0U;
    bool fixpoint = false;

    for (; step < squaring_count; ++step) {
        multiplyM4RMSerial(relation, relation, scratch_matrix, tables, num_tables);
        const bool identical = bitMatricesEqual(relation, scratch_matrix);
        std::swap(relation, scratch_matrix);
        if (identical) {
            fixpoint = true;
            ++step;
            break;
        }
    }

    if (executed_squarings != nullptr) {
        *executed_squarings = step;
    }
    if (reached_fixpoint != nullptr) {
        *reached_fixpoint = fixpoint;
    }
}

void BooleanClosure::transitiveClosureKleeneSquaringInPlace(
    BitMatrix& relation,
    const uint32_t squaring_count,
    BitMatrix* scratch,
    const KleeneSquaringOptions options,
    uint32_t* executed_squarings,
    bool* reached_fixpoint
) {
    if (executed_squarings != nullptr) {
        *executed_squarings = 0U;
    }
    if (reached_fixpoint != nullptr) {
        *reached_fixpoint = false;
    }

    const uint32_t num_vertices = relation.numRows();
    if (num_vertices != relation.numCols() || squaring_count == 0U) {
        if (reached_fixpoint != nullptr) {
            *reached_fixpoint = true;
        }
        return;
    }

    const ClosureAlgorithm resolved_alg = resolveClosureAlgorithm(options, num_vertices);

    if (resolved_alg == ClosureAlgorithm::Warshall) {
        transitiveClosureWarshallInPlace(relation);
        if (executed_squarings != nullptr) {
            *executed_squarings = 1U;
        }
        if (reached_fixpoint != nullptr) {
            *reached_fixpoint = true;
        }
        return;
    }

    if (resolved_alg == ClosureAlgorithm::M4RM4Table) {
        transitiveClosureM4RMSquaringInPlace(
            relation,
            squaring_count,
            scratch,
            4U,
            options,
            executed_squarings,
            reached_fixpoint
        );
        return;
    }

    if (resolved_alg == ClosureAlgorithm::M4RM8Table) {
        transitiveClosureM4RMSquaringInPlace(
            relation,
            squaring_count,
            scratch,
            8U,
            options,
            executed_squarings,
            reached_fixpoint
        );
        return;
    }

    // Fallback: Legacy Gustavson repeated squaring
    BitMatrix owned_scratch{};
    BitMatrix* effective_scratch = (scratch == &relation) ? nullptr : scratch;
    BitMatrix& scratch_matrix = ensureScratch(owned_scratch, effective_scratch, num_vertices);

    for (uint32_t step = 0U; step < squaring_count; ++step) {
        if (executed_squarings != nullptr) {
            *executed_squarings = step + 1U;
        }
        if (transitiveClosureKleeneSquaringStepInPlace(relation, scratch_matrix, options)) {
            if (reached_fixpoint != nullptr) {
                *reached_fixpoint = true;
            }
            return;
        }
    }
}

bool BooleanClosure::transitiveClosureKleeneSquaringStepInPlace(
    BitMatrix& relation,
    BitMatrix& scratch,
    const KleeneSquaringOptions options
) {
    const uint32_t num_vertices = relation.numRows();
    if (num_vertices != relation.numCols()) {
        return true;
    }

    BitMatrix owned_scratch{};
    BitMatrix* effective_scratch = (&scratch == &relation) ? nullptr : &scratch;
    BitMatrix& scratch_matrix = ensureScratch(owned_scratch, effective_scratch, num_vertices);
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

    const uint32_t num_blocks = (num_vertices + 63U) / 64U;
    for (uint32_t block = 0U; block < num_blocks; ++block) {
        const uint32_t pivot_start = block * 64U;
        const uint32_t pivot_end = std::min(pivot_start + 64U, num_vertices);

        // Phase 1: Close transitive reachability within the diagonal block of pivots.
        for (uint32_t pivot = pivot_start; pivot < pivot_end; ++pivot) {
            const BitVector& pivot_row = relation.row(pivot);
            const size_t pivot_word_index = static_cast<size_t>(pivot) / 64U;
            const uint64_t pivot_mask = 1ULL << (static_cast<size_t>(pivot) % 64U);

            for (uint32_t row_index = pivot_start; row_index < pivot_end; ++row_index) {
                if (row_index == pivot) {
                    continue;
                }
                if ((relation.row(row_index).word(pivot_word_index) & pivot_mask) != 0U) {
                    relation.row(row_index).rowOr(pivot_row);
                }
            }
        }

        // Phase 2: Stream all other rows once for all 64 pivots simultaneously.
        const uint32_t block_bits = pivot_end - pivot_start;
        const uint64_t block_mask =
            block_bits == 64U ? ~0ULL : ((1ULL << block_bits) - 1ULL);

        for (uint32_t row_index = 0U; row_index < num_vertices; ++row_index) {
            if (row_index >= pivot_start && row_index < pivot_end) {
                continue;
            }
            uint64_t reachable = relation.row(row_index).word(block) & block_mask;
            while (reachable != 0U) {
                const unsigned bit = static_cast<unsigned>(std::countr_zero(reachable));
                relation.row(row_index).rowOr(relation.row(pivot_start + bit));
                reachable &= reachable - 1ULL;
            }
        }
    }
}

BitMatrix BooleanClosure::transitiveClosureWarshall(BitMatrix relation) {
    transitiveClosureWarshallInPlace(relation);
    return relation;
}

}  // namespace hbrick
