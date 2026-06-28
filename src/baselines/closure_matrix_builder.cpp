#include "hbrick/baselines/closure_matrix_builder.hpp"

#include <stdexcept>

#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/graph/connected_components.hpp"

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
    const uint32_t largest_component_size =
        largestUndirectedComponentSize(graph);
    const uint32_t squaring_count =
        BooleanClosure::kleeneSquaringCountForLargestComponent(largest_component_size);
    BooleanClosure::transitiveClosureKleeneSquaringInPlace(
        reflexive_relation,
        squaring_count,
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
