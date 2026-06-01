#pragma once

#include "hbrick/graph/directed_grid_graph.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/passable_grid.hpp"

namespace hbrick {

class DirectedGridGraphBuilder {
public:
    [[nodiscard]] static DirectedGridGraph build(
        const PassableGrid& grid,
        GridEdgeConversionMode mode,
        RandomAsymmetricParams params = {}
    );
};

}  // namespace hbrick
