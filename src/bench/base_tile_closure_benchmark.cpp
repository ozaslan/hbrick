/**
 * @file base_tile_closure_benchmark.cpp
 * @brief Benchmark comparing Warshall-Floyd, All-Pairs BFS, and Serial/Parallel Repeated Matrix Squaring.
 *
 * Evaluates transitive closure performance across different tile dimensions (b = 8, 16, 32)
 * and super-tile interface port dimensions (gamma = 128, 256, 512, 1024, 2048, 4096).
 * Features multi-threaded parallel matrix squaring scaling from max available CPU cores (T = 8),
 * halved down to 2 threads (T = 8, 4, 2).
 */

#include <algorithm>
#include <atomic>
#include <barrier>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "hbrick/bench/bench_timer.hpp"
#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/tile/tile_closure_util.hpp"

namespace hbrick {
namespace {

enum class TileTopology {
    Open,
    Obstacles20,
    Maze
};

std::string topologyName(const TileTopology topo) {
    switch (topo) {
        case TileTopology::Open: return "Open Grid";
        case TileTopology::Obstacles20: return "20% Obstacles";
        case TileTopology::Maze: return "Maze Corridors";
    }
    return "Unknown";
}

struct TileInstance {
    uint32_t b = 0;
    uint32_t num_vertices = 0;
    TileTopology topology = TileTopology::Open;
    CsrGraph graph;
    BitMatrix initial_reflexive_adjacency;
};

TileInstance generateTileInstance(
    const uint32_t b,
    const TileTopology topology,
    const uint64_t seed = 42U
) {
    TileInstance inst;
    inst.b = b;
    inst.topology = topology;

    std::mt19937_64 rng(seed + b * 1000U + static_cast<uint64_t>(topology));
    std::uniform_real_distribution<double> dist01(0.0, 1.0);

    std::vector<bool> passable(static_cast<size_t>(b) * b, true);
    if (topology == TileTopology::Obstacles20) {
        for (uint32_t y = 0; y < b; ++y) {
            for (uint32_t x = 0; x < b; ++x) {
                if (dist01(rng) < 0.20) {
                    passable[static_cast<size_t>(y) * b + x] = false;
                }
            }
        }
    } else if (topology == TileTopology::Maze) {
        for (uint32_t y = 0; y < b; ++y) {
            for (uint32_t x = 0; x < b; ++x) {
                if ((y % 2 == 1) && (x % 4 != (y % 4))) {
                    passable[static_cast<size_t>(y) * b + x] = false;
                }
            }
        }
    }

    std::vector<int32_t> cell_to_vertex(static_cast<size_t>(b) * b, -1);
    uint32_t vertex_count = 0;
    for (size_t i = 0; i < passable.size(); ++i) {
        if (passable[i]) {
            cell_to_vertex[i] = static_cast<int32_t>(vertex_count++);
        }
    }
    inst.num_vertices = vertex_count;

    CsrGraphBuilder builder{vertex_count};
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};

    for (uint32_t y = 0; y < b; ++y) {
        for (uint32_t x = 0; x < b; ++x) {
            const size_t u_cell = static_cast<size_t>(y) * b + x;
            const int32_t u = cell_to_vertex[u_cell];
            if (u < 0) {
                continue;
            }

            for (int dir = 0; dir < 4; ++dir) {
                const int nx = static_cast<int>(x) + dx[dir];
                const int ny = static_cast<int>(y) + dy[dir];
                if (nx < 0 || nx >= static_cast<int>(b) || ny < 0 || ny >= static_cast<int>(b)) {
                    continue;
                }
                const size_t v_cell = static_cast<size_t>(ny) * b + static_cast<size_t>(nx);
                const int32_t v = cell_to_vertex[v_cell];
                if (v < 0) {
                    continue;
                }

                if (dist01(rng) < 0.85) {
                    builder.addEdge(static_cast<uint32_t>(u), static_cast<uint32_t>(v));
                }
            }
        }
    }

    inst.graph = builder.build();
    inst.initial_reflexive_adjacency = buildTileReflexiveAdjacencyOrThrow(
        inst.graph,
        std::numeric_limits<uint64_t>::max()
    );
    return inst;
}

void runOptimizedAllPairsBfs(
    const CsrGraph& graph,
    BitMatrix& out_matrix,
    GraphSearchScratch& scratch
) noexcept {
    const uint32_t num_vertices = graph.numVertices();
    std::vector<uint32_t>& visited = scratch.visitedMark();
    std::vector<uint32_t>& queue = scratch.queue();

    for (uint32_t source = 0U; source < num_vertices; ++source) {
        BitVector& source_row = out_matrix.row(source);
        source_row.clear();
        const uint32_t mark = scratch.nextMark();
        queue.clear();

        visited[source] = mark;
        queue.push_back(source);
        source_row.set(source);

        size_t head = 0U;
        while (head < queue.size()) {
            const uint32_t u = queue[head++];
            for (const uint32_t v : graph.outNeighbors(u)) {
                if (visited[v] != mark) {
                    visited[v] = mark;
                    queue.push_back(v);
                    source_row.set(v);
                }
            }
        }
    }
}

// --- Micro-Kernel Helpers for Boolean Matrix Squaring ---

void accumulateBooleanProductRowLocal(
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

bool rowsDifferLocal(const BitVector& lhs, const BitVector& rhs) noexcept {
    const size_t num_words = lhs.numWords();
    for (size_t word_index = 0U; word_index < num_words; ++word_index) {
        if (lhs.word(word_index) != rhs.word(word_index)) {
            return true;
        }
    }
    return false;
}

// --- M4RM (Method of Four Russians) Micro-Kernel Helpers ---

void multiplyBooleanM4RMLocal(
    const BitMatrix& lhs,
    const BitMatrix& rhs,
    BitMatrix& out,
    std::vector<BitVector>& table
) {
    const uint32_t n = lhs.numRows();
    out = lhs; // start with reflexive base

    const size_t num_stripes = (static_cast<size_t>(n) + 7U) / 8U;

    for (size_t stripe = 0; stripe < num_stripes; ++stripe) {
        const uint32_t pivot_start = static_cast<uint32_t>(stripe * 8U);
        const uint32_t k = std::min(8U, n - pivot_start);

        // Build 2^k table entries via prefix doubling
        table[0].clear();
        for (uint32_t b = 0; b < k; ++b) {
            const BitVector& r = rhs.row(pivot_start + b);
            const uint32_t half = 1U << b;
            for (uint32_t idx = 0; idx < half; ++idx) {
                table[half | idx] = table[idx];
                table[half | idx].rowOr(r);
            }
        }

        const uint8_t mask = (k == 8U) ? 0xFF : static_cast<uint8_t>((1U << k) - 1U);

        // Stream all rows of lhs
        for (uint32_t row = 0; row < n; ++row) {
            const uint8_t* const row_bytes = reinterpret_cast<const uint8_t*>(lhs.row(row).wordsData());
            const uint8_t code = row_bytes[stripe] & mask;
            if (code != 0U) {
                out.row(row).rowOr(table[code]);
            }
        }
    }
}

void transitiveClosureM4RMSquaringInPlace(
    BitMatrix& relation,
    const uint32_t max_rounds,
    BitMatrix& scratch,
    std::vector<BitVector>& table,
    uint32_t* actual_rounds = nullptr,
    bool* reached_fixpoint = nullptr
) {
    const uint32_t n = relation.numRows();
    if (n <= 1U || max_rounds == 0U) {
        if (reached_fixpoint != nullptr) *reached_fixpoint = true;
        if (actual_rounds != nullptr) *actual_rounds = 0U;
        return;
    }

    uint32_t step = 0;
    bool fixpoint = false;

    for (; step < max_rounds; ++step) {
        multiplyBooleanM4RMLocal(relation, relation, scratch, table);
        const bool identical = bitMatricesEqual(relation, scratch);
        std::swap(relation, scratch);
        if (identical) {
            fixpoint = true;
            ++step;
            break;
        }
    }

    if (actual_rounds != nullptr) *actual_rounds = step;
    if (reached_fixpoint != nullptr) *reached_fixpoint = fixpoint;
}

// --- M4RM Octa-Table (M4RI 8-Table, 64-bit word-parallel streaming) ---

void multiplyBooleanM4RMOctaLocal(
    const BitMatrix& lhs,
    const BitMatrix& rhs,
    BitMatrix& out,
    std::vector<std::vector<BitVector>>& tables
) {
    const uint32_t n = lhs.numRows();
    out = lhs; // start with reflexive base
    const size_t num_stripes = (static_cast<size_t>(n) + 7U) / 8U;
    const size_t num_words = (static_cast<size_t>(n) + 63U) / 64U;

    for (size_t word_idx = 0; word_idx < num_words; ++word_idx) {
        const size_t stripe_base = word_idx * 8U;
        const size_t active_tables = (stripe_base + 8 <= num_stripes)
            ? 8
            : (num_stripes > stripe_base ? num_stripes - stripe_base : 0);

        for (size_t t = 0; t < active_tables; ++t) {
            const uint32_t p = static_cast<uint32_t>((stripe_base + t) * 8U);
            const uint32_t k = std::min(8U, n - p);
            auto& tbl = tables[t];
            tbl[0].clear();
            for (uint32_t b = 0; b < k; ++b) {
                const BitVector& r = rhs.row(p + b);
                const uint32_t half = 1U << b;
                for (uint32_t idx = 0; idx < half; ++idx) {
                    tbl[half | idx] = tbl[idx];
                    tbl[half | idx].rowOr(r);
                }
            }
        }

        if (active_tables == 8) {
            for (uint32_t row = 0; row < n; ++row) {
                const uint64_t w = lhs.row(row).word(word_idx);
                if (w == 0ULL) continue;

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
            for (uint32_t row = 0; row < n; ++row) {
                const uint64_t w = lhs.row(row).word(word_idx);
                if (w == 0ULL) continue;
                BitVector& out_r = out.row(row);
                for (size_t t = 0; t < active_tables; ++t) {
                    const uint8_t code = static_cast<uint8_t>((w >> (t * 8U)) & 0xFF);
                    if (code != 0U) out_r.rowOr(tables[t][code]);
                }
            }
        }
    }
}

void transitiveClosureM4RMOctaSquaringInPlace(
    BitMatrix& relation,
    const uint32_t max_rounds,
    BitMatrix& scratch,
    std::vector<std::vector<BitVector>>& tables,
    uint32_t* actual_rounds = nullptr,
    bool* reached_fixpoint = nullptr
) {
    const uint32_t n = relation.numRows();
    if (n <= 1U || max_rounds == 0U) {
        if (reached_fixpoint != nullptr) *reached_fixpoint = true;
        if (actual_rounds != nullptr) *actual_rounds = 0U;
        return;
    }

    uint32_t step = 0;
    bool fixpoint = false;

    for (; step < max_rounds; ++step) {
        multiplyBooleanM4RMOctaLocal(relation, relation, scratch, tables);
        const bool identical = bitMatricesEqual(relation, scratch);
        std::swap(relation, scratch);
        if (identical) {
            fixpoint = true;
            ++step;
            break;
        }
    }

    if (actual_rounds != nullptr) *actual_rounds = step;
    if (reached_fixpoint != nullptr) *reached_fixpoint = fixpoint;
}

/**
 * @brief High-performance persistent thread pool for multi-threaded transitive closure.
 * Supports parallel 64-way blocked Warshall-Floyd, parallel Gustavson squaring, and parallel M4RM squaring.
 * Spawns worker threads once and reuses them across rounds via C++20 barriers.
 */
class ParallelClosureThreadPool {
public:
    enum class Task {
        Squaring,
        WarshallPhase2,
        M4RM,
        M4RMOcta,
        Stop
    };

    explicit ParallelClosureThreadPool(uint32_t num_threads)
        : num_threads_(num_threads),
          start_barrier_(num_threads + 1),
          done_barrier_(num_threads + 1),
          step_barrier_(num_threads),
          worker_changed_(num_threads, false),
          current_task_(Task::Squaring) {
        workers_.reserve(num_threads);
        for (uint32_t t = 0; t < num_threads; ++t) {
            workers_.emplace_back([this, t]() {
                workerLoop(t);
            });
        }
    }

    ~ParallelClosureThreadPool() {
        current_task_ = Task::Stop;
        start_barrier_.arrive_and_wait();
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
    }

    void parallelSquaringInPlace(
        BitMatrix& relation,
        uint32_t max_squarings,
        BitMatrix& scratch,
        uint32_t* actual_rounds = nullptr,
        bool* reached_fixpoint = nullptr
    ) {
        const uint32_t num_rows = relation.numRows();
        if (num_rows <= 1U || max_squarings == 0U) {
            if (reached_fixpoint != nullptr) *reached_fixpoint = true;
            if (actual_rounds != nullptr) *actual_rounds = 0U;
            return;
        }

        current_lhs_ = &relation;
        current_scratch_ = &scratch;
        num_rows_ = num_rows;
        current_task_ = Task::Squaring;

        uint32_t step = 0;
        bool fixpoint = false;

        for (; step < max_squarings; ++step) {
            start_barrier_.arrive_and_wait();
            done_barrier_.arrive_and_wait();

            bool any_changed = false;
            for (uint32_t t = 0; t < num_threads_; ++t) {
                if (worker_changed_[t]) {
                    any_changed = true;
                    break;
                }
            }

            if (!any_changed) {
                fixpoint = true;
                ++step;
                break;
            }

            std::swap(relation, scratch);
            current_lhs_ = &relation;
            current_scratch_ = &scratch;
        }

        if (actual_rounds != nullptr) {
            *actual_rounds = step;
        }
        if (reached_fixpoint != nullptr) {
            *reached_fixpoint = fixpoint;
        }
    }

    void parallelM4RMSquaringInPlace(
        BitMatrix& relation,
        BitMatrix& scratch,
        uint32_t max_squarings,
        uint32_t* actual_rounds = nullptr,
        bool* reached_fixpoint = nullptr
    ) {
        const uint32_t n = relation.numRows();
        if (n <= 1U || max_squarings == 0U) {
            if (reached_fixpoint != nullptr) *reached_fixpoint = true;
            if (actual_rounds != nullptr) *actual_rounds = 0U;
            return;
        }

        current_lhs_ = &relation;
        current_scratch_ = &scratch;
        num_rows_ = n;
        current_task_ = Task::M4RM;

        if (thread_tables_.size() != num_threads_) {
            thread_tables_.resize(num_threads_);
            for (uint32_t t = 0; t < num_threads_; ++t) {
                thread_tables_[t].resize(256, BitVector(n));
            }
        } else if (thread_tables_[0].empty() || thread_tables_[0][0].numBits() != n) {
            for (uint32_t t = 0; t < num_threads_; ++t) {
                thread_tables_[t].assign(256, BitVector(n));
            }
        }

        uint32_t step = 0;
        bool fixpoint = false;

        for (; step < max_squarings; ++step) {
            start_barrier_.arrive_and_wait();
            done_barrier_.arrive_and_wait();

            const bool identical = bitMatricesEqual(relation, scratch);
            std::swap(relation, scratch);
            current_lhs_ = &relation;
            current_scratch_ = &scratch;

            if (identical) {
                fixpoint = true;
                ++step;
                break;
            }
        }

        if (actual_rounds != nullptr) {
            *actual_rounds = step;
        }
        if (reached_fixpoint != nullptr) {
            *reached_fixpoint = fixpoint;
        }
    }

    void parallelM4RMOctaSquaringInPlace(
        BitMatrix& relation,
        BitMatrix& scratch,
        uint32_t max_squarings,
        uint32_t* actual_rounds = nullptr,
        bool* reached_fixpoint = nullptr
    ) {
        const uint32_t n = relation.numRows();
        if (n <= 1U || max_squarings == 0U) {
            if (reached_fixpoint != nullptr) *reached_fixpoint = true;
            if (actual_rounds != nullptr) *actual_rounds = 0U;
            return;
        }

        current_lhs_ = &relation;
        current_scratch_ = &scratch;
        num_rows_ = n;
        current_task_ = Task::M4RMOcta;

        if (shared_octa_tables_.size() != 8) {
            shared_octa_tables_.resize(8);
            for (size_t t = 0; t < 8; ++t) {
                shared_octa_tables_[t].resize(256, BitVector(n));
            }
        } else if (shared_octa_tables_[0].empty() || shared_octa_tables_[0][0].numBits() != n) {
            for (size_t t = 0; t < 8; ++t) {
                shared_octa_tables_[t].assign(256, BitVector(n));
            }
        }

        uint32_t step = 0;
        bool fixpoint = false;

        for (; step < max_squarings; ++step) {
            start_barrier_.arrive_and_wait();
            done_barrier_.arrive_and_wait();

            const bool identical = bitMatricesEqual(relation, scratch);
            std::swap(relation, scratch);
            current_lhs_ = &relation;
            current_scratch_ = &scratch;

            if (identical) {
                fixpoint = true;
                ++step;
                break;
            }
        }

        if (actual_rounds != nullptr) {
            *actual_rounds = step;
        }
        if (reached_fixpoint != nullptr) {
            *reached_fixpoint = fixpoint;
        }
    }

    void parallelWarshallInPlace(BitMatrix& relation) {
        const uint32_t num_vertices = relation.numRows();
        if (num_vertices <= 1U) {
            return;
        }

        const size_t num_blocks = (static_cast<size_t>(num_vertices) + 63U) / 64U;
        current_relation_ = &relation;
        num_rows_ = num_vertices;
        current_task_ = Task::WarshallPhase2;

        for (size_t block = 0U; block < num_blocks; ++block) {
            const uint32_t pivot_start = static_cast<uint32_t>(block * 64U);
            const uint32_t pivot_end = std::min(pivot_start + 64U, num_vertices);

            // Phase 1: Close transitive reachability within the diagonal block of pivots (done by main thread)
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

            // Phase 2: Stream all other rows in parallel across worker threads
            current_block_ = block;
            current_pivot_start_ = pivot_start;
            current_pivot_end_ = pivot_end;

            start_barrier_.arrive_and_wait();
            done_barrier_.arrive_and_wait();
        }
    }

private:
    void workerLoop(uint32_t thread_id) {
        while (true) {
            start_barrier_.arrive_and_wait();
            if (current_task_ == Task::Stop) {
                break;
            }

            const uint32_t rows_per_thread = (num_rows_ + num_threads_ - 1U) / num_threads_;
            const uint32_t row_begin = thread_id * rows_per_thread;
            const uint32_t row_end = std::min(row_begin + rows_per_thread, num_rows_);

            if (current_task_ == Task::Squaring) {
                bool local_changed = false;
                for (uint32_t row = row_begin; row < row_end; ++row) {
                    accumulateBooleanProductRowLocal(
                        current_lhs_->row(row),
                        *current_lhs_,
                        current_scratch_->row(row),
                        num_rows_
                    );
                    if (rowsDifferLocal(current_scratch_->row(row), current_lhs_->row(row))) {
                        local_changed = true;
                    }
                }
                worker_changed_[thread_id] = local_changed;
            } else if (current_task_ == Task::M4RM) {
                auto& table = thread_tables_[thread_id];
                const size_t num_stripes = (static_cast<size_t>(num_rows_) + 7U) / 8U;

                for (uint32_t r = row_begin; r < row_end; ++r) {
                    current_scratch_->row(r) = current_lhs_->row(r);
                }

                for (size_t stripe = 0; stripe < num_stripes; ++stripe) {
                    const uint32_t pivot_start = static_cast<uint32_t>(stripe * 8U);
                    const uint32_t k = std::min(8U, num_rows_ - pivot_start);

                    table[0].clear();
                    for (uint32_t b = 0; b < k; ++b) {
                        const BitVector& r = current_lhs_->row(pivot_start + b);
                        const uint32_t half = 1U << b;
                        for (uint32_t idx = 0; idx < half; ++idx) {
                            table[half | idx] = table[idx];
                            table[half | idx].rowOr(r);
                        }
                    }

                    const uint8_t mask = (k == 8U) ? 0xFF : static_cast<uint8_t>((1U << k) - 1U);

                    for (uint32_t row = row_begin; row < row_end; ++row) {
                        const uint8_t* const row_bytes = reinterpret_cast<const uint8_t*>(current_lhs_->row(row).wordsData());
                        const uint8_t code = row_bytes[stripe] & mask;
                        if (code != 0U) {
                            current_scratch_->row(row).rowOr(table[code]);
                        }
                    }
                }
            } else if (current_task_ == Task::M4RMOcta) {
                const size_t num_stripes = (static_cast<size_t>(num_rows_) + 7U) / 8U;
                const size_t num_words = (static_cast<size_t>(num_rows_) + 63U) / 64U;

                for (uint32_t r = row_begin; r < row_end; ++r) {
                    current_scratch_->row(r) = current_lhs_->row(r);
                }

                for (size_t word_idx = 0; word_idx < num_words; ++word_idx) {
                    const size_t stripe_base = word_idx * 8U;
                    const size_t active_tables = (stripe_base + 8 <= num_stripes)
                        ? 8
                        : (num_stripes > stripe_base ? num_stripes - stripe_base : 0);

                    for (size_t t = thread_id; t < active_tables; t += num_threads_) {
                        const uint32_t p = static_cast<uint32_t>((stripe_base + t) * 8U);
                        const uint32_t k = std::min(8U, num_rows_ - p);
                        auto& tbl = shared_octa_tables_[t];
                        tbl[0].clear();
                        for (uint32_t b = 0; b < k; ++b) {
                            const BitVector& r = current_lhs_->row(p + b);
                            const uint32_t half = 1U << b;
                            for (uint32_t idx = 0; idx < half; ++idx) {
                                tbl[half | idx] = tbl[idx];
                                tbl[half | idx].rowOr(r);
                            }
                        }
                    }

                    step_barrier_.arrive_and_wait();

                    if (active_tables == 8) {
                        for (uint32_t row = row_begin; row < row_end; ++row) {
                            const uint64_t w = current_lhs_->row(row).word(word_idx);
                            if (w == 0ULL) continue;
                            BitVector& out_r = current_scratch_->row(row);
                            if (w & 0xFF) out_r.rowOr(shared_octa_tables_[0][static_cast<uint8_t>(w & 0xFF)]);
                            if ((w >> 8) & 0xFF) out_r.rowOr(shared_octa_tables_[1][static_cast<uint8_t>((w >> 8) & 0xFF)]);
                            if ((w >> 16) & 0xFF) out_r.rowOr(shared_octa_tables_[2][static_cast<uint8_t>((w >> 16) & 0xFF)]);
                            if ((w >> 24) & 0xFF) out_r.rowOr(shared_octa_tables_[3][static_cast<uint8_t>((w >> 24) & 0xFF)]);
                            if ((w >> 32) & 0xFF) out_r.rowOr(shared_octa_tables_[4][static_cast<uint8_t>((w >> 32) & 0xFF)]);
                            if ((w >> 40) & 0xFF) out_r.rowOr(shared_octa_tables_[5][static_cast<uint8_t>((w >> 40) & 0xFF)]);
                            if ((w >> 48) & 0xFF) out_r.rowOr(shared_octa_tables_[6][static_cast<uint8_t>((w >> 48) & 0xFF)]);
                            if ((w >> 56) & 0xFF) out_r.rowOr(shared_octa_tables_[7][static_cast<uint8_t>((w >> 56) & 0xFF)]);
                        }
                    } else {
                        for (uint32_t row = row_begin; row < row_end; ++row) {
                            const uint64_t w = current_lhs_->row(row).word(word_idx);
                            if (w == 0ULL) continue;
                            BitVector& out_r = current_scratch_->row(row);
                            for (size_t t = 0; t < active_tables; ++t) {
                                const uint8_t code = static_cast<uint8_t>((w >> (t * 8U)) & 0xFF);
                                if (code != 0U) out_r.rowOr(shared_octa_tables_[t][code]);
                            }
                        }
                    }

                    step_barrier_.arrive_and_wait();
                }
            } else if (current_task_ == Task::WarshallPhase2) {
                const uint32_t block_bits = current_pivot_end_ - current_pivot_start_;
                const uint64_t block_mask =
                    block_bits == 64U ? ~0ULL : ((1ULL << block_bits) - 1ULL);

                for (uint32_t row_index = row_begin; row_index < row_end; ++row_index) {
                    if (row_index >= current_pivot_start_ && row_index < current_pivot_end_) {
                        continue;
                    }
                    uint64_t reachable = current_relation_->row(row_index).word(current_block_) & block_mask;
                    while (reachable != 0U) {
                        const unsigned bit = static_cast<unsigned>(std::countr_zero(reachable));
                        current_relation_->row(row_index).rowOr(current_relation_->row(current_pivot_start_ + bit));
                        reachable &= reachable - 1ULL;
                    }
                }
            }

            done_barrier_.arrive_and_wait();
        }
    }

    uint32_t num_threads_;
    std::barrier<> start_barrier_;
    std::barrier<> done_barrier_;
    std::barrier<> step_barrier_;
    std::vector<bool> worker_changed_;
    std::vector<std::thread> workers_;
    std::atomic<Task> current_task_;

    // Squaring state
    const BitMatrix* current_lhs_ = nullptr;
    BitMatrix* current_scratch_ = nullptr;
    uint32_t num_rows_ = 0U;

    // M4RM state
    std::vector<std::vector<BitVector>> thread_tables_;
    std::vector<std::vector<BitVector>> shared_octa_tables_;

    // Warshall state
    BitMatrix* current_relation_ = nullptr;
    size_t current_block_ = 0U;
    uint32_t current_pivot_start_ = 0U;
    uint32_t current_pivot_end_ = 0U;
};

struct BenchResult {
    double min_us = 0.0;
    double p50_us = 0.0;
    double p90_us = 0.0;
    double mean_us = 0.0;
    double ops_per_sec = 0.0;
    uint32_t executed_rounds = 0;
    bool early_fixpoint = false;
};

template <typename Func>
BenchResult timeOperation(
    const uint32_t trials,
    const uint32_t warmup,
    Func&& func
) {
    for (uint32_t w = 0; w < warmup; ++w) {
        func();
    }

    std::vector<double> samples;
    samples.reserve(trials);

    for (uint32_t t = 0; t < trials; ++t) {
        const auto start = std::chrono::steady_clock::now();
        func();
        const auto end = std::chrono::steady_clock::now();
        const double duration_us = std::chrono::duration<double, std::micro>(end - start).count();
        samples.push_back(duration_us);
    }

    std::sort(samples.begin(), samples.end());
    BenchResult res;
    res.min_us = samples.front();
    res.p50_us = samples[samples.size() / 2];
    res.p90_us = samples[static_cast<size_t>(0.90 * static_cast<double>(samples.size() - 1))];

    double sum = 0.0;
    for (const double s : samples) {
        sum += s;
    }
    res.mean_us = sum / static_cast<double>(samples.size());
    res.ops_per_sec = (res.p50_us > 0.0) ? (1.0e6 / res.p50_us) : 0.0;
    return res;
}

void printHeader() {
    std::cout << "\n"
              << std::string(124, '=') << "\n"
              << std::left
              << std::setw(8)  << "Tile b"
              << std::setw(14) << "Topology"
              << std::setw(10) << "Verts d"
              << std::setw(18) << "All-Pairs BFS"
              << std::setw(18) << "Warshall-Floyd"
              << std::setw(22) << "Squaring (Fixpoint)"
              << std::setw(22) << "Squaring (ceil log2)"
              << std::setw(14) << "Fastest Method"
              << "\n"
              << std::string(124, '-') << "\n";
}

void runBenchmarkSuite(
    const std::vector<uint32_t>& tile_sizes,
    const std::vector<TileTopology>& topologies,
    const uint32_t base_trials
) {
    printHeader();

    for (const uint32_t b : tile_sizes) {
        for (const TileTopology topo : topologies) {
            const TileInstance inst = generateTileInstance(b, topo);
            const uint32_t d = inst.num_vertices;
            if (d == 0U) {
                continue;
            }

            const uint32_t log2_d = (d <= 1U) ? 0U : static_cast<uint32_t>(std::bit_width(d - 1U));

            uint32_t trials = base_trials;
            uint32_t warmup = 5;
            if (d <= 64) {
                trials = base_trials * 5;
                warmup = 10;
            } else if (d > 1024) {
                trials = std::max(5U, base_trials / 5);
                warmup = 2;
            }

            // 1. All-Pairs BFS
            BitMatrix bfs_matrix(d, d);
            GraphSearchScratch bfs_scratch(d);
            BenchResult res_bfs = timeOperation(trials, warmup, [&]() {
                runOptimizedAllPairsBfs(inst.graph, bfs_matrix, bfs_scratch);
            });

            // 2. Warshall-Floyd
            BitMatrix warshall_matrix = inst.initial_reflexive_adjacency;
            BenchResult res_warshall = timeOperation(trials, warmup, [&]() {
                warshall_matrix = inst.initial_reflexive_adjacency;
                BooleanClosure::transitiveClosureWarshallInPlace(warshall_matrix);
            });

            // 3. Matrix Squaring with Early Fixpoint
            BitMatrix kleene_matrix = inst.initial_reflexive_adjacency;
            BitMatrix kleene_scratch(d, d);
            uint32_t executed_squarings = 0U;
            bool reached_fixpoint = false;

            BenchResult res_squaring_fix = timeOperation(trials, warmup, [&]() {
                kleene_matrix = inst.initial_reflexive_adjacency;
                BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                    kleene_matrix,
                    log2_d,
                    &kleene_scratch,
                    KleeneSquaringOptions{.algorithm = ClosureAlgorithm::GustavsonSquaring},
                    &executed_squarings,
                    &reached_fixpoint
                );
            });
            res_squaring_fix.executed_rounds = executed_squarings;
            res_squaring_fix.early_fixpoint = reached_fixpoint;

            // 4. Matrix Squaring Fixed ceil(log2(d)) steps (No Early Exit)
            BitMatrix kleene_fixed = inst.initial_reflexive_adjacency;
            BitMatrix kleene_fixed_scratch(d, d);
            BenchResult res_squaring_fixed = timeOperation(trials, warmup, [&]() {
                kleene_fixed = inst.initial_reflexive_adjacency;
                for (uint32_t step = 0; step < log2_d; ++step) {
                    (void)booleanMultiplyInto(kleene_fixed, kleene_fixed, kleene_fixed_scratch, {});
                    std::swap(kleene_fixed, kleene_fixed_scratch);
                }
            });

            const bool warshall_ok = bitMatricesEqual(bfs_matrix, warshall_matrix);
            const bool kleene_ok = bitMatricesEqual(bfs_matrix, kleene_matrix);
            const bool kleene_fixed_ok = bitMatricesEqual(bfs_matrix, kleene_fixed);

            if (!warshall_ok || !kleene_ok || !kleene_fixed_ok) {
                std::cerr << "\n[ERROR] Result mismatch detected for b=" << b
                          << " topology=" << topologyName(topo) << "!\n";
                std::exit(EXIT_FAILURE);
            }

            std::string fastest = "BFS";
            double best_p50 = res_bfs.p50_us;
            if (res_warshall.p50_us < best_p50) {
                best_p50 = res_warshall.p50_us;
                fastest = "Warshall";
            }
            if (res_squaring_fix.p50_us < best_p50) {
                best_p50 = res_squaring_fix.p50_us;
                fastest = "Squaring (Fix)";
            }

            std::cout << std::left
                      << std::setw(8)  << b
                      << std::setw(14) << topologyName(topo).substr(0, 13)
                      << std::setw(10) << d
                      << std::setw(18) << (std::to_string(static_cast<uint64_t>(res_bfs.p50_us)) + " us")
                      << std::setw(18) << (std::to_string(static_cast<uint64_t>(res_warshall.p50_us)) + " us")
                      << std::setw(22) << (std::to_string(static_cast<uint64_t>(res_squaring_fix.p50_us)) + " us (" + std::to_string(executed_squarings) + "/" + std::to_string(log2_d) + ")")
                      << std::setw(22) << (std::to_string(static_cast<uint64_t>(res_squaring_fixed.p50_us)) + " us (" + std::to_string(log2_d) + " steps)")
                      << std::setw(14) << fastest
                      << "\n";
        }
        std::cout << std::string(124, '-') << "\n";
    }
}

BitMatrix generateSuperTileInterfaceMatrix(const uint32_t gamma, const uint64_t seed = 42U) {
    BitMatrix matrix(gamma, gamma);
    for (uint32_t i = 0; i < gamma; ++i) {
        matrix.set(i, i);
    }
    const uint32_t num_children = 4;
    const uint32_t k = gamma / num_children;
    std::mt19937_64 rng(seed + gamma);
    std::uniform_real_distribution<double> dist01(0.0, 1.0);

    for (uint32_t c = 0; c < num_children; ++c) {
        const uint32_t offset = c * k;
        BitMatrix child_rel(k, k);
        for (uint32_t i = 0; i < k; ++i) {
            child_rel.set(i, i);
            for (uint32_t j = 0; j < k; ++j) {
                if (i != j && dist01(rng) < 0.25) {
                    child_rel.set(i, j);
                }
            }
        }
        BooleanClosure::transitiveClosureWarshallInPlace(child_rel);
        for (uint32_t i = 0; i < k; ++i) {
            for (uint32_t j = 0; j < k; ++j) {
                if (child_rel.test(i, j)) {
                    matrix.set(offset + i, offset + j);
                }
            }
        }
    }

    const std::vector<std::pair<uint32_t, uint32_t>> seams = {
        {0, 1}, {0, 2}, {1, 3}, {2, 3}
    };
    const uint32_t seam_width = std::max(2U, k / 4U);
    for (const auto& [c1, c2] : seams) {
        const uint32_t off1 = c1 * k;
        const uint32_t off2 = c2 * k;
        for (uint32_t s = 0; s < seam_width; ++s) {
            matrix.set(off1 + s, off2 + s);
            matrix.set(off2 + s, off1 + s);
        }
    }
    return matrix;
}

void runSuperTileBenchmarkSuite(const std::vector<uint32_t>& gamma_sizes, const uint32_t base_trials) {
    std::cout << "\n========================================================================================================================================\n"
              << "          H-BRICK SUPER-TILE INTERFACE CLOSURE BENCHMARK (gamma_U ports, 4 children + seams)\n"
              << "========================================================================================================================================================\n"
              << std::left
              << std::setw(20) << "Level / Gamma"
              << std::setw(15) << "Warshall-Floyd"
              << std::setw(20) << "Squaring (Gustav)"
              << std::setw(18) << "Squaring (M4RM)"
              << std::setw(20) << "Squaring (Octa)"
              << std::setw(20) << "Squaring (ceil log)"
              << std::setw(15) << "Fastest Method"
              << std::setw(10) << "Speedup"
              << "\n"
              << std::string(140, '-') << "\n";

    for (const uint32_t gamma : gamma_sizes) {
        const BitMatrix initial_matrix = generateSuperTileInterfaceMatrix(gamma);
        const uint32_t log2_gamma = (gamma <= 1U) ? 0U : static_cast<uint32_t>(std::bit_width(gamma - 1U));

        uint32_t trials = base_trials;
        uint32_t warmup = 5;
        if (gamma <= 256) {
            trials = base_trials * 3;
            warmup = 10;
        } else if (gamma >= 2048) {
            trials = std::max(5U, base_trials / 5);
            warmup = 2;
        }

        // 1. Warshall
        BitMatrix warshall_matrix = initial_matrix;
        BenchResult res_warshall = timeOperation(trials, warmup, [&]() {
            warshall_matrix = initial_matrix;
            BooleanClosure::transitiveClosureWarshallInPlace(warshall_matrix);
        });

        // 2. Squaring (Gustavson with early fixpoint)
        BitMatrix kleene_matrix = initial_matrix;
        BitMatrix kleene_scratch(gamma, gamma);
        uint32_t actual_rounds = 0U;
        bool reached_fixpoint = false;

        BenchResult res_squaring_fix = timeOperation(trials, warmup, [&]() {
            kleene_matrix = initial_matrix;
            BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                kleene_matrix,
                log2_gamma,
                &kleene_scratch,
                KleeneSquaringOptions{.algorithm = ClosureAlgorithm::GustavsonSquaring},
                &actual_rounds,
                &reached_fixpoint
            );
        });

        // 3. Squaring (M4RM with early fixpoint)
        BitMatrix m4rm_matrix = initial_matrix;
        BitMatrix m4rm_scratch(gamma, gamma);
        std::vector<BitVector> m4rm_table(256, BitVector(gamma));
        uint32_t m4rm_rounds = 0U;
        bool m4rm_fixpoint = false;

        BenchResult res_m4rm = timeOperation(trials, warmup, [&]() {
            m4rm_matrix = initial_matrix;
            transitiveClosureM4RMSquaringInPlace(
                m4rm_matrix,
                log2_gamma,
                m4rm_scratch,
                m4rm_table,
                &m4rm_rounds,
                &m4rm_fixpoint
            );
        });

        // 3b. Squaring (M4RM Octa-Table with early fixpoint)
        BitMatrix m4rm_octa_matrix = initial_matrix;
        BitMatrix m4rm_octa_scratch(gamma, gamma);
        std::vector<std::vector<BitVector>> m4rm_octa_tables(8, std::vector<BitVector>(256, BitVector(gamma)));
        uint32_t m4rm_octa_rounds = 0U;
        bool m4rm_octa_fixpoint = false;

        BenchResult res_m4rm_octa = timeOperation(trials, warmup, [&]() {
            m4rm_octa_matrix = initial_matrix;
            transitiveClosureM4RMOctaSquaringInPlace(
                m4rm_octa_matrix,
                log2_gamma,
                m4rm_octa_scratch,
                m4rm_octa_tables,
                &m4rm_octa_rounds,
                &m4rm_octa_fixpoint
            );
        });

        // 4. Squaring fixed (exact ceil(log2(gamma)) steps)
        BitMatrix kleene_fixed = initial_matrix;
        BitMatrix kleene_fixed_scratch(gamma, gamma);
        BenchResult res_squaring_fixed = timeOperation(trials, warmup, [&]() {
            kleene_fixed = initial_matrix;
            for (uint32_t step = 0; step < log2_gamma; ++step) {
                (void)booleanMultiplyInto(kleene_fixed, kleene_fixed, kleene_fixed_scratch, {});
                std::swap(kleene_fixed, kleene_fixed_scratch);
            }
        });

        if (!bitMatricesEqual(warshall_matrix, kleene_matrix) ||
            !bitMatricesEqual(warshall_matrix, m4rm_matrix) ||
            !bitMatricesEqual(warshall_matrix, m4rm_octa_matrix) ||
            !bitMatricesEqual(warshall_matrix, kleene_fixed)) {
            std::cerr << "Mismatch in super-tile closure for gamma=" << gamma << "!\n";
            std::exit(EXIT_FAILURE);
        }

        std::string fastest = "Warshall";
        double fastest_time = res_warshall.p50_us;
        if (res_m4rm.p50_us < fastest_time) {
            fastest_time = res_m4rm.p50_us;
            fastest = "Squar (M4RM)";
        }
        if (res_m4rm_octa.p50_us < fastest_time) {
            fastest_time = res_m4rm_octa.p50_us;
            fastest = "Squar (Octa)";
        }
        if (res_squaring_fix.p50_us < fastest_time) {
            fastest_time = res_squaring_fix.p50_us;
            fastest = "Squar (Gust)";
        }

        std::string speedup_str;
        if (fastest == "Warshall") {
            const double sp = res_m4rm_octa.p50_us / res_warshall.p50_us;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << sp << "x";
            speedup_str = ss.str();
        } else {
            const double sp = res_warshall.p50_us / fastest_time;
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << sp << "x";
            speedup_str = ss.str();
        }

        std::string level_desc = "gamma=" + std::to_string(gamma);
        if (gamma == 128) level_desc += " (L1,b8)";
        else if (gamma == 256) level_desc += " (L1,b16)";
        else if (gamma == 512) level_desc += " (L2,b16)";
        else if (gamma == 1024) level_desc += " (L3,b16)";
        else if (gamma == 2048) level_desc += " (L4,b16)";
        else if (gamma == 4096) level_desc += " (L4,b32)";

        std::cout << std::left
                  << std::setw(20) << level_desc
                  << std::setw(15) << (std::to_string(static_cast<uint64_t>(res_warshall.p50_us)) + " us")
                  << std::setw(20) << (std::to_string(static_cast<uint64_t>(res_squaring_fix.p50_us)) + " us (" + std::to_string(actual_rounds) + " r)")
                  << std::setw(18) << (std::to_string(static_cast<uint64_t>(res_m4rm.p50_us)) + " us (" + std::to_string(m4rm_rounds) + " r)")
                  << std::setw(20) << (std::to_string(static_cast<uint64_t>(res_m4rm_octa.p50_us)) + " us (" + std::to_string(m4rm_octa_rounds) + " r)")
                  << std::setw(20) << (std::to_string(static_cast<uint64_t>(res_squaring_fixed.p50_us)) + " us (" + std::to_string(log2_gamma) + " st)")
                  << std::setw(15) << fastest
                  << std::setw(10) << speedup_str
                  << "\n";
    }
    std::cout << std::string(140, '-') << "\n";
}

/**
 * @brief Fair Benchmark evaluating parallel scaling of BOTH Warshall-Floyd and Matrix Squaring.
 * Tests thread counts: 1T, 2T, 4T, 8T on the exact same matrices, topology, and CPU cores.
 */
void runParallelClosureComparisonBenchmark(
    const std::vector<uint32_t>& gamma_sizes,
    const std::vector<uint32_t>& thread_counts,
    const uint32_t base_trials
) {
    std::cout << "\n========================================================================================================================================\n"
              << "       FAIR BENCHMARK: PARALLEL WARSHALL-FLOYD vs PARALLEL MATRIX SQUARING (1T, 2T, 4T, 8T SWEEP)\n"
              << "========================================================================================================================================\n"
              << std::left
              << std::setw(14) << "Dimension"
              << std::setw(12) << "Algorithm"
              << std::setw(16) << "1 Thread"
              << std::setw(16) << "2 Threads"
              << std::setw(16) << "4 Threads"
              << std::setw(16) << "8 Threads"
              << std::setw(18) << "Scaling (8T/1T)"
              << std::setw(24) << "Head-to-Head (8T)"
              << "\n"
              << std::string(132, '-') << "\n";

    for (const uint32_t gamma : gamma_sizes) {
        const BitMatrix initial_matrix = generateSuperTileInterfaceMatrix(gamma);
        const uint32_t log2_gamma = (gamma <= 1U) ? 0U : static_cast<uint32_t>(std::bit_width(gamma - 1U));

        uint32_t trials = base_trials;
        uint32_t warmup = 3;
        if (gamma <= 256) {
            trials = base_trials * 2;
            warmup = 5;
        } else if (gamma >= 2048) {
            trials = std::max(5U, base_trials / 3);
            warmup = 1;
        }

        // 1. Warshall 1T baseline
        BitMatrix warshall_matrix = initial_matrix;
        BenchResult res_warshall_1t = timeOperation(trials, warmup, [&]() {
            warshall_matrix = initial_matrix;
            BooleanClosure::transitiveClosureWarshallInPlace(warshall_matrix);
        });

        // 2. Gustavson Squaring 1T baseline
        BitMatrix kleene_1t = initial_matrix;
        BitMatrix scratch_1t(gamma, gamma);
        uint32_t rounds_1t = 0;
        bool fixpoint_1t = false;
        BenchResult res_squaring_1t = timeOperation(trials, warmup, [&]() {
            kleene_1t = initial_matrix;
            BooleanClosure::transitiveClosureKleeneSquaringInPlace(
                kleene_1t,
                log2_gamma,
                &scratch_1t,
                KleeneSquaringOptions{.algorithm = ClosureAlgorithm::GustavsonSquaring},
                &rounds_1t,
                &fixpoint_1t
            );
        });

        // 3. M4RM Squaring 1T baseline
        BitMatrix m4rm_1t = initial_matrix;
        BitMatrix m4rm_scratch_1t(gamma, gamma);
        std::vector<BitVector> m4rm_table_1t(256, BitVector(gamma));
        uint32_t m4rm_rounds_1t = 0;
        bool m4rm_fixpoint_1t = false;
        BenchResult res_m4rm_1t = timeOperation(trials, warmup, [&]() {
            m4rm_1t = initial_matrix;
            transitiveClosureM4RMSquaringInPlace(
                m4rm_1t,
                log2_gamma,
                m4rm_scratch_1t,
                m4rm_table_1t,
                &m4rm_rounds_1t,
                &m4rm_fixpoint_1t
            );
        });

        // 4. M4RM-Octa Squaring 1T baseline
        BitMatrix m4rm_octa_1t = initial_matrix;
        BitMatrix m4rm_octa_scratch_1t(gamma, gamma);
        std::vector<std::vector<BitVector>> m4rm_octa_tables_1t(8, std::vector<BitVector>(256, BitVector(gamma)));
        uint32_t m4rm_octa_rounds_1t = 0;
        bool m4rm_octa_fixpoint_1t = false;
        BenchResult res_m4rm_octa_1t = timeOperation(trials, warmup, [&]() {
            m4rm_octa_1t = initial_matrix;
            transitiveClosureM4RMOctaSquaringInPlace(
                m4rm_octa_1t,
                log2_gamma,
                m4rm_octa_scratch_1t,
                m4rm_octa_tables_1t,
                &m4rm_octa_rounds_1t,
                &m4rm_octa_fixpoint_1t
            );
        });

        std::vector<double> warshall_times = {res_warshall_1t.p50_us};
        std::vector<double> gustavson_times = {res_squaring_1t.p50_us};
        std::vector<double> m4rm_times = {res_m4rm_1t.p50_us};
        std::vector<double> m4rm_octa_times = {res_m4rm_octa_1t.p50_us};

        // Run multi-threaded benchmarks for each thread count (e.g. 2, 4, 8)
        // Scoped pool ensures ONLY t threads exist at any point in time!
        for (const uint32_t t : thread_counts) {
            ParallelClosureThreadPool pool(t);

            // Parallel Warshall
            BitMatrix warshall_par = initial_matrix;
            BenchResult res_w_par = timeOperation(trials, warmup, [&]() {
                warshall_par = initial_matrix;
                pool.parallelWarshallInPlace(warshall_par);
            });
            if (!bitMatricesEqual(warshall_matrix, warshall_par)) {
                std::cerr << "Mismatch in parallel Warshall for gamma=" << gamma << " with " << t << " threads!\n";
                std::exit(EXIT_FAILURE);
            }
            warshall_times.push_back(res_w_par.p50_us);

            // Parallel Gustavson Squaring
            BitMatrix kleene_par = initial_matrix;
            BitMatrix scratch_par(gamma, gamma);
            uint32_t par_rounds = 0;
            bool par_fixpoint = false;
            BenchResult res_s_par = timeOperation(trials, warmup, [&]() {
                kleene_par = initial_matrix;
                pool.parallelSquaringInPlace(
                    kleene_par,
                    log2_gamma,
                    scratch_par,
                    &par_rounds,
                    &par_fixpoint
                );
            });
            if (!bitMatricesEqual(warshall_matrix, kleene_par)) {
                std::cerr << "Mismatch in parallel squaring for gamma=" << gamma << " with " << t << " threads!\n";
                std::exit(EXIT_FAILURE);
            }
            gustavson_times.push_back(res_s_par.p50_us);

            // Parallel M4RM Squaring
            BitMatrix m4rm_par = initial_matrix;
            BitMatrix m4rm_scratch_par(gamma, gamma);
            uint32_t m4rm_par_rounds = 0;
            bool m4rm_par_fixpoint = false;
            BenchResult res_m_par = timeOperation(trials, warmup, [&]() {
                m4rm_par = initial_matrix;
                pool.parallelM4RMSquaringInPlace(
                    m4rm_par,
                    m4rm_scratch_par,
                    log2_gamma,
                    &m4rm_par_rounds,
                    &m4rm_par_fixpoint
                );
            });
            if (!bitMatricesEqual(warshall_matrix, m4rm_par)) {
                std::cerr << "Mismatch in parallel M4RM squaring for gamma=" << gamma << " with " << t << " threads!\n";
                std::exit(EXIT_FAILURE);
            }
            m4rm_times.push_back(res_m_par.p50_us);

            // Parallel M4RM-Octa Squaring
            BitMatrix m4rm_octa_par = initial_matrix;
            BitMatrix m4rm_octa_scratch_par(gamma, gamma);
            uint32_t m4rm_octa_par_rounds = 0;
            bool m4rm_octa_par_fixpoint = false;
            BenchResult res_mo_par = timeOperation(trials, warmup, [&]() {
                m4rm_octa_par = initial_matrix;
                pool.parallelM4RMOctaSquaringInPlace(
                    m4rm_octa_par,
                    m4rm_octa_scratch_par,
                    log2_gamma,
                    &m4rm_octa_par_rounds,
                    &m4rm_octa_par_fixpoint
                );
            });
            if (!bitMatricesEqual(warshall_matrix, m4rm_octa_par)) {
                std::cerr << "Mismatch in parallel M4RM-Octa squaring for gamma=" << gamma << " with " << t << " threads!\n";
                std::exit(EXIT_FAILURE);
            }
            m4rm_octa_times.push_back(res_mo_par.p50_us);
        }

        const double w_8t = warshall_times.back();
        const double g_8t = gustavson_times.back();
        const double m_8t = m4rm_times.back();
        const double mo_8t = m4rm_octa_times.back();

        std::ostringstream ss_w_sc, ss_g_sc, ss_m_sc, ss_mo_sc;
        ss_w_sc << std::fixed << std::setprecision(2) << (warshall_times[0] / w_8t) << "x";
        ss_g_sc << std::fixed << std::setprecision(2) << (gustavson_times[0] / g_8t) << "x";
        ss_m_sc << std::fixed << std::setprecision(2) << (m4rm_times[0] / m_8t) << "x";
        ss_mo_sc << std::fixed << std::setprecision(2) << (m4rm_octa_times[0] / mo_8t) << "x";

        std::string head_to_head;
        if (mo_8t <= w_8t) {
            const double sp = w_8t / mo_8t;
            std::ostringstream ss;
            ss << "Octa " << std::fixed << std::setprecision(2) << sp << "x FASTER";
            head_to_head = ss.str();
        } else {
            const double sp = mo_8t / w_8t;
            std::ostringstream ss;
            ss << "Warshall " << std::fixed << std::setprecision(2) << sp << "x faster";
            head_to_head = ss.str();
        }

        std::string dim_desc = "gamma=" + std::to_string(gamma);

        // Row 1: Warshall
        std::cout << std::left
                  << std::setw(14) << dim_desc
                  << std::setw(14) << "Warshall"
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(warshall_times[0])) + " us")
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(warshall_times[1])) + " us")
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(warshall_times[2])) + " us")
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(warshall_times[3])) + " us")
                  << std::setw(18) << ss_w_sc.str()
                  << std::setw(24) << head_to_head
                  << "\n";

        // Row 2: Gustavson Squaring
        std::cout << std::left
                  << std::setw(14) << ""
                  << std::setw(14) << "Squar-Gust"
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(gustavson_times[0])) + " us")
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(gustavson_times[1])) + " us")
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(gustavson_times[2])) + " us")
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(gustavson_times[3])) + " us")
                  << std::setw(18) << ss_g_sc.str()
                  << std::setw(24) << ""
                  << "\n";

        // Row 3: M4RM Squaring
        std::cout << std::left
                  << std::setw(14) << ""
                  << std::setw(14) << "Squar-M4RM"
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(m4rm_times[0])) + " us")
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(m4rm_times[1])) + " us")
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(m4rm_times[2])) + " us")
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(m4rm_times[3])) + " us")
                  << std::setw(18) << ss_m_sc.str()
                  << std::setw(24) << ""
                  << "\n";

        // Row 4: M4RM-Octa Squaring
        std::cout << std::left
                  << std::setw(14) << ""
                  << std::setw(14) << "Squar-Octa"
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(m4rm_octa_times[0])) + " us")
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(m4rm_octa_times[1])) + " us")
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(m4rm_octa_times[2])) + " us")
                  << std::setw(16) << (std::to_string(static_cast<uint64_t>(m4rm_octa_times[3])) + " us")
                  << std::setw(18) << ss_mo_sc.str()
                  << std::setw(24) << ""
                  << "\n";

        std::cout << std::string(132, '-') << "\n";
    }
}

}  // namespace
}  // namespace hbrick

int main(int argc, char** argv) {
    std::vector<uint32_t> tile_sizes = {8, 16, 32};
    std::vector<hbrick::TileTopology> topologies = {
        hbrick::TileTopology::Open,
        hbrick::TileTopology::Obstacles20,
        hbrick::TileTopology::Maze
    };
    uint32_t trials = 15;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: base_tile_closure_benchmark [options]\n"
                      << "Options:\n"
                      << "  --b <sizes>        Comma-separated tile sizes b (default: 8,16,32)\n"
                      << "  --trials <n>       Number of timing repetitions (default: 15)\n"
                      << "  --help, -h         Show this message\n";
            return 0;
        }
        if (arg == "--trials" && i + 1 < argc) {
            trials = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--b" && i + 1 < argc) {
            tile_sizes.clear();
            std::stringstream ss(argv[++i]);
            std::string item;
            while (std::getline(ss, item, ',')) {
                if (!item.empty()) {
                    tile_sizes.push_back(static_cast<uint32_t>(std::atoi(item.c_str())));
                }
            }
        }
    }

    std::cout << "========================================================================================\n"
              << "       H-BRICK BASE TILE TRANSITIVE CLOSURE BENCHMARK SUITE\n"
              << "========================================================================================\n"
              << "Comparing:\n"
              << " 1. All-Pairs BFS (CSR, O(1) timestamp marks, zero-allocation preallocated scratch)\n"
              << " 2. Warshall-Floyd (64-way block streaming, bit-parallel AVX2 row-OR, zero pruning)\n"
              << " 3. Matrix Squaring Gustavson (H-BRICK repeated squaring with early fixpoint detection)\n"
              << " 4. Matrix Squaring M4RM (Method of Four Russians byte-aligned prefix-doubling)\n"
              << " 5. Matrix Squaring Fixed (Exact ceil(log2(d)) full Boolean multiplications)\n"
              << "========================================================================================\n";

    hbrick::runBenchmarkSuite(tile_sizes, topologies, trials);

    std::vector<uint32_t> gamma_sizes = {128, 256, 512, 1024, 2048, 4096};
    hbrick::runSuperTileBenchmarkSuite(gamma_sizes, trials);

    // Compute thread counts: max CPU cores, halved down to 2 threads
    const uint32_t hw_threads = std::thread::hardware_concurrency();
    std::vector<uint32_t> thread_counts;
    // User specification: "kac islemci varsa o kadar paralel olsun. sonra onun yarisi olsun taa ki 2 tane kalana kadar"
    // e.g. for 8 cores: 2, 4, 8
    for (uint32_t t = 2; t <= hw_threads; t *= 2) {
        thread_counts.push_back(t);
    }

    std::vector<uint32_t> par_gammas = {256, 512, 1024, 2048, 4096};
    hbrick::runParallelClosureComparisonBenchmark(par_gammas, thread_counts, trials);

    std::cout << "\nBenchmark completed successfully with bit-level validation across all methods and thread counts.\n";
    return 0;
}
