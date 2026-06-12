#include "hbrick/graph/directed_grid_graph_builder.hpp"

#include <cmath>
#include <limits>
#include <numbers>
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

void buildBidirectionalAll(const MazeLayout& grid, CsrGraphBuilder& builder) {
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

void buildAcyclicEastSouth(const MazeLayout& grid, CsrGraphBuilder& builder) {
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
    const MazeLayout& grid,
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

void buildGradientFlow(
    const MazeLayout& grid,
    CsrGraphBuilder& builder,
    const RandomAsymmetricParams& params
) {
    std::mt19937_64 rng{params.seed};

    const uint64_t threshold_bi =
        static_cast<uint64_t>(params.p_bidirectional * kMaxU64AsLongDouble);
    const uint64_t threshold_flip =
        static_cast<uint64_t>(params.p_against_gradient * kMaxU64AsLongDouble);

    const double angle_radians =
        params.gradient_angle_degrees * std::numbers::pi / 180.0;
    const double flow_x = std::cos(angle_radians);
    const double flow_y = std::sin(angle_radians);

    grid.forEachPassableAdjacentPairEastSouth([&](
        const GridCoord from,
        const GridCoord to,
        const Direction direction
    ) {
        const uint32_t from_vertex = grid.vertexId(from).value;
        const uint32_t to_vertex = grid.vertexId(to).value;

        if (rng() < threshold_bi) {
            addBidirectionalEdge(builder, from_vertex, to_vertex);
            return;
        }

        // East steps project onto +x, south steps onto +y (row grows downward).
        const double projection =
            direction == Direction::East ? flow_x : flow_y;

        bool along_flow;
        constexpr double kPerpendicularEpsilon = 1e-9;
        if (projection > kPerpendicularEpsilon) {
            along_flow = true;
        } else if (projection < -kPerpendicularEpsilon) {
            along_flow = false;
        } else {
            along_flow = (rng() & 1U) == 0U;
        }

        if (rng() < threshold_flip) {
            along_flow = !along_flow;
        }

        if (along_flow) {
            builder.addEdge(from_vertex, to_vertex);
        } else {
            builder.addEdge(to_vertex, from_vertex);
        }
    });
}

}  // namespace

DirectedGridGraph DirectedGridGraphBuilder::build(
    const MazeLayout& grid,
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
        case GridEdgeConversionMode::GradientFlow:
            buildGradientFlow(grid, builder, params);
            break;
    }

    return DirectedGridGraph{grid.width(), grid.height(), builder.build()};
}

}  // namespace hbrick
