#include "hbrick/graph/directed_grid_graph_builder.hpp"

#include <limits>
#include <random>

#include "hbrick/graph/csr_graph_builder.hpp"

namespace hbrick {

namespace {

constexpr long double kMaxU64AsLongDouble =
    static_cast<long double>(std::numeric_limits<uint64_t>::max());

void addBidirectionalEdge(CsrGraphBuilder& builder, const uint32_t from, const uint32_t to) {
    builder.addEdge(from, to);
    builder.addEdge(to, from);
}

void buildBidirectionalAll(const PassableGrid& grid, CsrGraphBuilder& builder) {
    grid.forEachPassableAdjacentPairEastSouth([&builder, &grid](
        const GridCoord from,
        const GridCoord to,
        const Direction
    ) {
        const uint32_t from_vertex = grid.vertexId(from).value;
        const uint32_t to_vertex = grid.vertexId(to).value;
        addBidirectionalEdge(builder, from_vertex, to_vertex);
    });
}

void buildAcyclicEastSouth(const PassableGrid& grid, CsrGraphBuilder& builder) {
    grid.forEachPassableAdjacentPairEastSouth([&builder, &grid](
        const GridCoord from,
        const GridCoord to,
        const Direction
    ) {
        const uint32_t from_vertex = grid.vertexId(from).value;
        const uint32_t to_vertex = grid.vertexId(to).value;
        builder.addEdge(from_vertex, to_vertex);
    });
}

void buildRandomAsymmetric(
    const PassableGrid& grid,
    CsrGraphBuilder& builder,
    const RandomAsymmetricParams& params
) {
    std::mt19937_64 rng{params.seed};

    const uint64_t threshold_bi =
        static_cast<uint64_t>(params.p_bidirectional * kMaxU64AsLongDouble);
    const uint64_t threshold_one = static_cast<uint64_t>(
        (params.p_bidirectional + params.p_one_way) * kMaxU64AsLongDouble
    );

    grid.forEachPassableAdjacentPairEastSouth([&](
        const GridCoord from,
        const GridCoord to,
        const Direction
    ) {
        const uint32_t from_vertex = grid.vertexId(from).value;
        const uint32_t to_vertex = grid.vertexId(to).value;

        const uint64_t decision = rng();
        if (decision < threshold_bi) {
            addBidirectionalEdge(builder, from_vertex, to_vertex);
            return;
        }

        if (decision < threshold_one) {
            const uint64_t orientation = rng();
            if ((orientation & 1U) == 0U) {
                builder.addEdge(from_vertex, to_vertex);
            } else {
                builder.addEdge(to_vertex, from_vertex);
            }
        }
    });
}

}  // namespace

DirectedGridGraph DirectedGridGraphBuilder::build(
    const PassableGrid& grid,
    const GridEdgeConversionMode mode,
    RandomAsymmetricParams params
) {
    CsrGraphBuilder builder{grid.numVertices()};

    switch (mode) {
        case GridEdgeConversionMode::BidirectionalAll:
            buildBidirectionalAll(grid, builder);
            break;
        case GridEdgeConversionMode::AcyclicEastSouth:
            buildAcyclicEastSouth(grid, builder);
            break;
        case GridEdgeConversionMode::RandomAsymmetric:
            buildRandomAsymmetric(grid, builder, params);
            break;
    }

    return DirectedGridGraph{grid.width(), grid.height(), builder.build()};
}

}  // namespace hbrick
