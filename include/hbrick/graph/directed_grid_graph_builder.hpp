/**
 * @file directed_grid_graph_builder.hpp
 * @ingroup hbrick_graph
 * @brief Factory for building @ref hbrick::DirectedGridGraph from @ref hbrick::PassableGrid inputs.
 */

#pragma once

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/passable_grid.hpp"

namespace hbrick {

/**
 * @brief Converts passable grid adjacencies into a directed grid graph.
 * @ingroup hbrick_graph
 *
 * Supports several edge-orientation policies selected by @ref hbrick::GridEdgeConversionMode,
 * including fully bidirectional corridors, east/south acyclic orientations, and
 * seeded random asymmetric routing.
 */
class DirectedGridGraphBuilder {
public:
    /**
     * @brief Builds a directed graph from @p grid using @p mode.
     * @ingroup hbrick_graph
     *
     * @param grid Passable cell layout defining vertices and candidate edges.
     * @param mode Policy controlling how undirected adjacencies become arcs.
     * @param params Randomness parameters used when @p mode is
     *               @ref hbrick::GridEdgeConversionMode::RandomAsymmetric.
     * @return Grid graph with CSR storage and coordinate metadata.
     */
    [[nodiscard]] static DirectedGridGraph build(
        const PassableGrid& grid,
        GridEdgeConversionMode mode,
        RandomAsymmetricParams params = {}
    );
};

}  // namespace hbrick
