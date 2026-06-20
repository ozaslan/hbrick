#include "hbrick/tile/super_tile_composer.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/tile_boundary_order.hpp"
#include "hbrick/tile/tile_closure_util.hpp"
#include "hbrick/tile/tile_port.hpp"

namespace hbrick {

namespace {

[[nodiscard]] bool coordsEqual(const GridCoord lhs, const GridCoord rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] bool containsCoord(
    const std::span<const GridCoord> coords,
    const GridCoord target
) noexcept {
    for (const GridCoord coord : coords) {
        if (coordsEqual(coord, target)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] uint32_t gammaIndexForCoord(
    const GammaOrdering& gamma,
    const GridCoord coord
) noexcept {
    for (uint32_t index = 0U; index < gamma.ports.size(); ++index) {
        if (coordsEqual(gamma.ports[index], coord)) {
            return index;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

[[nodiscard]] uint32_t canonicalExteriorRank(
    const TileSlot& parent_bbox,
    const GridCoord coord
) noexcept {
    const std::vector<GridCoord> canonical = canonicalBoundaryCoords(parent_bbox);
    for (uint32_t index = 0U; index < canonical.size(); ++index) {
        if (coordsEqual(canonical[index], coord)) {
            return index;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

[[nodiscard]] bool compareGammaPortOrder(
    const TileSlot& parent_bbox,
    const GridCoord lhs,
    const GridCoord rhs
) noexcept {
    const bool lhs_exterior = isOnTilePerimeter(lhs, parent_bbox);
    const bool rhs_exterior = isOnTilePerimeter(rhs, parent_bbox);
    if (lhs_exterior != rhs_exterior) {
        return lhs_exterior;
    }

    if (lhs_exterior) {
        const uint32_t lhs_rank = canonicalExteriorRank(parent_bbox, lhs);
        const uint32_t rhs_rank = canonicalExteriorRank(parent_bbox, rhs);
        if (lhs_rank != rhs_rank) {
            return lhs_rank < rhs_rank;
        }
    }

    if (lhs.y != rhs.y) {
        return lhs.y < rhs.y;
    }
    return lhs.x < rhs.x;
}

[[nodiscard]] BitMatrix transpose(const BitMatrix& matrix) {
    BitMatrix transposed(matrix.numCols(), matrix.numRows());
    for (uint32_t row = 0U; row < matrix.numRows(); ++row) {
        for (uint32_t col = 0U; col < matrix.numCols(); ++col) {
            if (matrix.test(row, col)) {
                transposed.set(col, row);
            }
        }
    }
    return transposed;
}

[[nodiscard]] BitMatrix booleanMultiply(const BitMatrix& lhs, const BitMatrix& rhs) {
    if (lhs.numCols() != rhs.numRows()) {
        throw std::invalid_argument("booleanMultiply: inner dimensions must match");
    }

    BitMatrix product(lhs.numRows(), rhs.numCols());
    for (uint32_t row = 0U; row < lhs.numRows(); ++row) {
        for (uint32_t inner = 0U; inner < lhs.numCols(); ++inner) {
            if (!lhs.test(row, inner)) {
                continue;
            }
            product.row(row).rowOr(rhs.row(inner));
        }
    }
    return product;
}

void booleanOrInPlace(BitMatrix& target, const BitMatrix& source) {
    if (target.numRows() != source.numRows() || target.numCols() != source.numCols()) {
        throw std::invalid_argument("booleanOrInPlace: matrix dimensions must match");
    }

    for (uint32_t row = 0U; row < target.numRows(); ++row) {
        for (uint32_t col = 0U; col < target.numCols(); ++col) {
            if (source.test(row, col)) {
                target.set(row, col);
            }
        }
    }
}

[[nodiscard]] std::vector<GridCoord> collectExteriorPorts(
    const TileSlot& parent_bbox,
    const GammaOrdering& gamma
) {
    const std::vector<GridCoord> canonical = canonicalBoundaryCoords(parent_bbox);
    std::vector<GridCoord> exterior;
    exterior.reserve(canonical.size());

    for (const GridCoord coord : canonical) {
        if (containsCoord(gamma.ports, coord)) {
            exterior.push_back(coord);
        }
    }

    return exterior;
}

}  // namespace

GammaOrdering buildGammaOrdering(
    const TileSlot& parent_bbox,
    const std::span<const ChildBoundarySummary> children
) {
    GammaOrdering gamma;
    for (const ChildBoundarySummary& child : children) {
        for (const GridCoord coord : child.port_coords) {
            if (!parent_bbox.contains(coord)) {
                continue;
            }
            if (!containsCoord(gamma.ports, coord)) {
                gamma.ports.push_back(coord);
            }
        }
    }

    std::sort(
        gamma.ports.begin(),
        gamma.ports.end(),
        [&](const GridCoord lhs, const GridCoord rhs) noexcept {
            return compareGammaPortOrder(parent_bbox, lhs, rhs);
        }
    );

    return gamma;
}

BitMatrix buildEmbedding(
    const std::span<const GridCoord> child_port_coords,
    const GammaOrdering& gamma
) {
    BitMatrix embedding(
        static_cast<uint32_t>(gamma.ports.size()),
        static_cast<uint32_t>(child_port_coords.size())
    );
    for (uint32_t child_port = 0U; child_port < child_port_coords.size(); ++child_port) {
        const uint32_t gamma_index =
            gammaIndexForCoord(gamma, child_port_coords[child_port]);
        if (gamma_index != std::numeric_limits<uint32_t>::max()) {
            embedding.set(gamma_index, child_port);
        }
    }
    return embedding;
}

BitMatrix buildIfaceAdjacency(
    const GammaOrdering& gamma,
    const PortIndex& port_index,
    const std::span<const SeamEdge> seam_edges
) {
    const uint32_t gamma_size = static_cast<uint32_t>(gamma.ports.size());
    BitMatrix iface(gamma_size, gamma_size);
    for (const SeamEdge& seam_edge : seam_edges) {
        const PortRecord& from_record = port_index.port(seam_edge.from_port_id);
        const PortRecord& to_record = port_index.port(seam_edge.to_port_id);
        if (from_record.tile_index == to_record.tile_index) {
            continue;
        }

        const uint32_t from_gamma = gammaIndexForCoord(gamma, from_record.coord);
        const uint32_t to_gamma = gammaIndexForCoord(gamma, to_record.coord);
        if (from_gamma == std::numeric_limits<uint32_t>::max() ||
            to_gamma == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        iface.set(from_gamma, to_gamma);
    }
    return iface;
}

BitMatrix composeInterfaceAdjacency(
    const GammaOrdering& gamma,
    const std::span<const ChildBoundarySummary> children,
    const std::span<const BitMatrix> child_embeddings,
    const BitMatrix& iface_adjacency
) {
    if (children.size() != child_embeddings.size()) {
        throw std::invalid_argument(
            "composeInterfaceAdjacency: child and embedding counts must match"
        );
    }

    const uint32_t gamma_size = static_cast<uint32_t>(gamma.ports.size());
    BitMatrix composed(gamma_size, gamma_size);
    for (uint32_t index = 0U; index < gamma_size; ++index) {
        composed.set(index, index);
    }

    for (std::size_t child_index = 0U; child_index < children.size(); ++child_index) {
        const ChildBoundarySummary& child = children[child_index];
        const BitMatrix& embedding = child_embeddings[child_index];
        if (child.boundary_summary == nullptr) {
            throw std::invalid_argument("composeInterfaceAdjacency: missing child summary");
        }

        const BitMatrix child_contribution = booleanMultiply(
            booleanMultiply(embedding, *child.boundary_summary),
            transpose(embedding)
        );
        booleanOrInPlace(composed, child_contribution);
    }

    booleanOrInPlace(composed, iface_adjacency);
    return composed;
}

BitMatrix computeInterfaceClosure(BitMatrix composed_adjacency) {
    BooleanClosure::transitiveClosureWarshallInPlace(composed_adjacency);
    return composed_adjacency;
}

BitMatrix projectExternalSummary(
    const TileSlot& parent_bbox,
    const GammaOrdering& gamma,
    const BitMatrix& interface_closure
) {
    const std::vector<GridCoord> exterior_ports = collectExteriorPorts(parent_bbox, gamma);
    BitMatrix boundary(
        static_cast<uint32_t>(exterior_ports.size()),
        static_cast<uint32_t>(exterior_ports.size())
    );

    for (uint32_t from = 0U; from < exterior_ports.size(); ++from) {
        const uint32_t gamma_from = gammaIndexForCoord(gamma, exterior_ports[from]);
        for (uint32_t to = 0U; to < exterior_ports.size(); ++to) {
            const uint32_t gamma_to = gammaIndexForCoord(gamma, exterior_ports[to]);
            if (interface_closure.test(gamma_from, gamma_to)) {
                boundary.set(from, to);
            }
        }
    }

    return boundary;
}

SuperTileSummary composeSuperTile(
    const TileSlot& parent_bbox,
    const std::span<const ChildBoundarySummary> children,
    const PortIndex& port_index,
    const std::span<const SeamEdge> seam_edges,
    const uint64_t max_memory_bytes
) {
    SuperTileSummary result{};
    result.slot = parent_bbox;

    if (children.empty()) {
        result.status = BaselineStatus::Failed;
        return result;
    }

    for (const ChildBoundarySummary& child : children) {
        if (child.boundary_summary == nullptr || child.port_coords.empty()) {
            result.status = BaselineStatus::Failed;
            return result;
        }
    }

    result.gamma = buildGammaOrdering(parent_bbox, children);
    if (result.gamma.ports.empty()) {
        result.status = BaselineStatus::Failed;
        return result;
    }

    const uint32_t gamma_size = static_cast<uint32_t>(result.gamma.ports.size());
    if (!canAllocateTileReflexiveAdjacency(gamma_size, max_memory_bytes)) {
        result.status = BaselineStatus::SkippedByPolicy;
        return result;
    }

    result.child_embeddings.reserve(children.size());
    for (const ChildBoundarySummary& child : children) {
        result.child_embeddings.push_back(buildEmbedding(child.port_coords, result.gamma));
    }

    const BitMatrix iface_adjacency =
        buildIfaceAdjacency(result.gamma, port_index, seam_edges);
    BitMatrix composed = composeInterfaceAdjacency(
        result.gamma,
        children,
        result.child_embeddings,
        iface_adjacency
    );
    result.interface_closure = computeInterfaceClosure(std::move(composed));
    result.boundary_summary =
        projectExternalSummary(parent_bbox, result.gamma, result.interface_closure);

    result.exterior_ports = collectExteriorPorts(parent_bbox, result.gamma);
    result.exterior_gamma_indices.reserve(result.exterior_ports.size());
    for (const GridCoord coord : result.exterior_ports) {
        result.exterior_gamma_indices.push_back(gammaIndexForCoord(result.gamma, coord));
    }

    result.status = BaselineStatus::Completed;
    return result;
}

ChildBoundarySummary childBoundaryFromBaseTile(
    const BaseTileSummary& summary,
    const uint32_t tile_index,
    std::vector<GridCoord>& port_coord_storage
) {
    port_coord_storage.clear();
    port_coord_storage.reserve(summary.numPorts());
    for (const TilePort& port : summary.ports) {
        port_coord_storage.push_back(port.coord);
    }

    ChildBoundarySummary child{};
    child.slot = summary.slot;
    child.port_coords = port_coord_storage;
    child.boundary_summary = &summary.boundary_summary;
    child.tile_index = tile_index;
    return child;
}

ChildBoundarySummary childBoundaryFromSuperTile(
    const SuperTileSummary& summary,
    std::vector<GridCoord>& port_coord_storage
) {
    port_coord_storage = summary.gamma.ports;

    ChildBoundarySummary child{};
    child.slot = summary.slot;
    child.port_coords = port_coord_storage;
    child.boundary_summary = &summary.interface_closure;
    child.tile_index = std::numeric_limits<uint32_t>::max();
    return child;
}

}  // namespace hbrick
