/**
 * @file scc_compressed_closure.hpp
 * @ingroup hbrick_graph
 * @brief Kleene closure on SCC-compressed reflexive adjacency matrices.
 */

#pragma once

#include <cstdint>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/bit/kleene_squaring_options.hpp"
#include "hbrick/graph/csr_graph.hpp"
#include "hbrick/graph/scc_decomposition.hpp"

namespace hbrick {

class GraphSearchScratch;

/**
 * @brief Reflexive adjacency on SCC super-nodes plus original vertex labeling.
 * @ingroup hbrick_graph
 */
struct SccCompressedReflexiveAdjacency {
    /** @brief Vertex-to-component labeling for the original graph. @ingroup hbrick_graph */
    SccDecomposition decomposition;
    /** @brief Condensation DAG in CSR form (vertices are component ids). @ingroup hbrick_graph */
    CsrGraph condensation_dag;
    /** @brief Reflexive @c C x @c C adjacency on SCC representatives. @ingroup hbrick_graph */
    BitMatrix component_adjacency;
};

/**
 * @brief Returns whether SCC compression reduces the Kleene workspace.
 * @ingroup hbrick_graph
 */
[[nodiscard]] bool shouldUseSccCompressedClosure(
    uint32_t num_vertices,
    uint32_t num_components
) noexcept;

/**
 * @brief Builds a reflexive component-level adjacency from @p graph and @p decomposition.
 * @ingroup hbrick_graph
 */
[[nodiscard]] SccCompressedReflexiveAdjacency buildSccCompressedReflexiveAdjacency(
    const CsrGraph& graph,
    const SccDecomposition& decomposition
);

/**
 * @brief Builds a reflexive component-level adjacency from a reflexive vertex matrix.
 * @ingroup hbrick_graph
 */
[[nodiscard]] SccCompressedReflexiveAdjacency buildSccCompressedReflexiveAdjacency(
    const BitMatrix& reflexive_vertex_adjacency,
    const SccDecomposition& decomposition
);

/**
 * @brief Expands a component closure into a vertex-level closure matrix.
 * @ingroup hbrick_graph
 *
 * Sets @p vertex_closure_out[u][v] when @c u and @c v are in the same SCC or the
 * component closure reaches from @c component(u) to @c component(v).
 */
void expandComponentClosureToVertexClosure(
    const SccDecomposition& decomposition,
    const BitMatrix& component_closure,
    BitMatrix& vertex_closure_out
);

/**
 * @brief Answers reachability using a component closure and vertex-to-SCC labels.
 * @ingroup hbrick_graph
 */
[[nodiscard]] bool vertexReachableInComponentClosure(
    const SccDecomposition& decomposition,
    const BitMatrix& component_closure,
    uint32_t source_vertex,
    uint32_t target_vertex
) noexcept;

/**
 * @brief Computes vertex-level Kleene closure via SCC compression when beneficial.
 * @ingroup hbrick_graph
 *
 * When compression applies, runs truncated Kleene squaring on the @c C x @c C
 * component matrix and expands the result into @p reflexive_vertex_adjacency.
 * Otherwise falls back to direct vertex-level Kleene squaring on @p graph.
 *
 * @return @c true when the SCC-compressed path was used.
 */
[[nodiscard]] bool transitiveClosureKleeneSccCompressedInPlace(
    BitMatrix& reflexive_vertex_adjacency,
    const CsrGraph& graph,
    GraphSearchScratch& scratch,
    BitMatrix* multiply_scratch = nullptr,
    KleeneSquaringOptions options = {}
) noexcept;

/**
 * @brief Computes vertex-level Kleene closure via SCC compression when beneficial.
 * @ingroup hbrick_graph
 *
 * Derives directed edges from @p reflexive_vertex_adjacency, then delegates to the
 * CSR overload.
 */
[[nodiscard]] bool transitiveClosureKleeneSccCompressedInPlace(
    BitMatrix& reflexive_vertex_adjacency,
    GraphSearchScratch& scratch,
    BitMatrix* multiply_scratch = nullptr,
    KleeneSquaringOptions options = {}
) noexcept;

}  // namespace hbrick
