#include "hbrick/baselines/closure_matrix_builder.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/connected_components.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/scc_compressed_closure.hpp"

namespace hbrick {

namespace {

constexpr uint64_t kBitsPerWord = 64U;

[[nodiscard]] uint64_t wordsPerRow(const uint32_t num_vertices) noexcept {
    return (static_cast<uint64_t>(num_vertices) + (kBitsPerWord - 1U)) / kBitsPerWord;
}

}  // namespace

uint64_t ClosureMatrixBuilder::estimateReflexiveAdjacencyBytes(
    const uint32_t num_vertices
) noexcept {
    const uint64_t rows = num_vertices;
    return rows * wordsPerRow(num_vertices) * sizeof(uint64_t);
}

bool ClosureMatrixBuilder::canAllocateReflexiveAdjacency(
    const uint32_t num_vertices,
    const uint64_t max_memory_bytes
) noexcept {
    return estimateReflexiveAdjacencyBytes(num_vertices) <= max_memory_bytes;
}

uint64_t ClosureMatrixBuilder::estimateWorkingSetBytes(
    const uint32_t num_vertices,
    const uint32_t matrix_count,
    const uint32_t safety_percent
) noexcept {
    const uint64_t matrices = std::max<uint32_t>(1U, matrix_count);
    const uint64_t margin = std::max<uint32_t>(100U, safety_percent);
    const uint64_t base = estimateReflexiveAdjacencyBytes(num_vertices);
    if (base == 0U) {
        return 0U;
    }

    const uint64_t max_u64 = std::numeric_limits<uint64_t>::max();
    if (matrices > 1U && base > max_u64 / matrices) {
        return max_u64;
    }
    uint64_t total = base * matrices;

    const uint64_t extra_percent = margin > 100U ? margin - 100U : 0U;
    if (extra_percent == 0U) {
        return total;
    }

    // Ceiling of total * extra_percent / 100, computed without overflow.
    const uint64_t whole = total / 100U;
    const uint64_t remainder = total % 100U;
    uint64_t extra = 0U;
    if (whole > 0U && extra_percent > max_u64 / whole) {
        return max_u64;
    }
    extra = whole * extra_percent;
    const uint64_t remainder_extra = (remainder * extra_percent + 99U) / 100U;
    if (extra > max_u64 - remainder_extra) {
        return max_u64;
    }
    extra += remainder_extra;

    if (extra > max_u64 - total) {
        return max_u64;
    }
    total += extra;
    return total;
}

bool ClosureMatrixBuilder::canAllocateWorkingSet(
    const uint32_t num_vertices,
    const uint32_t matrix_count,
    const uint64_t max_memory_bytes,
    const uint32_t safety_percent
) noexcept {
    return estimateWorkingSetBytes(num_vertices, matrix_count, safety_percent)
        <= max_memory_bytes;
}

BitMatrix ClosureMatrixBuilder::buildReflexiveAdjacencyOrThrow(
    const CsrGraph& graph,
    const uint64_t max_memory_bytes
) {
    const uint32_t num_vertices = graph.numVertices();
    const uint64_t estimated_bytes = estimateReflexiveAdjacencyBytes(num_vertices);
    if (estimated_bytes > max_memory_bytes) {
        throw std::runtime_error(
            "Reflexive adjacency matrix exceeds configured memory limit"
        );
    }

    BitMatrix relation(num_vertices, num_vertices);

    for (uint32_t vertex = 0; vertex < num_vertices; ++vertex) {
        relation.set(vertex, vertex);
        for (const uint32_t neighbor : graph.outNeighbors(vertex)) {
            relation.set(vertex, neighbor);
        }
    }

    return relation;
}

void ClosureMatrixBuilder::transitiveClosureKleeneTruncatedInPlace(
    BitMatrix& reflexive_relation,
    const CsrGraph& graph,
    BitMatrix* scratch,
    const KleeneSquaringOptions options
) {
    GraphSearchScratch scc_scratch{graph.numVertices()};
    (void)transitiveClosureKleeneSccCompressedInPlace(
        reflexive_relation,
        graph,
        scc_scratch,
        scratch,
        options
    );
}

void ClosureMatrixBuilder::transitiveClosureWarshallOracleInPlace(
    BitMatrix& reflexive_relation
) {
    BooleanClosure::transitiveClosureWarshallInPlace(reflexive_relation);
}

}  // namespace hbrick
