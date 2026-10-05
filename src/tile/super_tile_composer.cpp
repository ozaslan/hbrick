#include "hbrick/tile/super_tile_composer.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>

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

[[nodiscard]] uint64_t packCoord(const GridCoord coord) noexcept {
    return (static_cast<uint64_t>(coord.y) << 32U) | static_cast<uint64_t>(coord.x);
}

/**
 * @brief Sorted (packed-coord → gamma-index) table for O(log |Γ|) lookups during compose.
 *
 * Preprocess-only; not used on the query hot path.
 */
class GammaCoordIndex {
public:
    explicit GammaCoordIndex(const GammaOrdering& gamma) {
        entries_.reserve(gamma.ports.size());
        for (uint32_t index = 0U; index < gamma.ports.size(); ++index) {
            entries_.push_back({packCoord(gamma.ports[index]), index});
        }
        std::sort(
            entries_.begin(),
            entries_.end(),
            [](const Entry& lhs, const Entry& rhs) noexcept {
                return lhs.key < rhs.key;
            }
        );
    }

    [[nodiscard]] uint32_t indexFor(const GridCoord coord) const noexcept {
        const uint64_t key = packCoord(coord);
        const auto it = std::lower_bound(
            entries_.begin(),
            entries_.end(),
            key,
            [](const Entry& entry, const uint64_t probe) noexcept {
                return entry.key < probe;
            }
        );
        if (it == entries_.end() || it->key != key) {
            return std::numeric_limits<uint32_t>::max();
        }
        return it->index;
    }

private:
    struct Entry {
        uint64_t key = 0U;
        uint32_t index = 0U;
    };

    std::vector<Entry> entries_;
};

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


void booleanOrInPlace(BitMatrix& target, const BitMatrix& source) {
    if (target.numRows() != source.numRows() || target.numCols() != source.numCols()) {
        throw std::invalid_argument("booleanOrInPlace: matrix dimensions must match");
    }

    for (uint32_t row = 0U; row < target.numRows(); ++row) {
        target.row(row).rowOr(source.row(row));
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

/**
 * @brief Scatters child summary bits into @p composed via port-coordinate lookup.
 *
 * Semantically identical to @c E * S * E^T for one-hot embeddings, without
 * allocating temporary product matrices.
 */
void scatterChildSummaryIntoGamma(
    BitMatrix& composed,
    const GammaCoordIndex& gamma_index,
    const std::span<const GridCoord> port_coords,
    const BitMatrix& child_summary
) {
    const uint32_t child_ports = static_cast<uint32_t>(port_coords.size());
    if (child_summary.numRows() != child_ports || child_summary.numCols() != child_ports) {
        throw std::invalid_argument(
            "scatterChildSummaryIntoGamma: child summary dims must match port count"
        );
    }

    std::vector<uint32_t> child_to_gamma(child_ports, std::numeric_limits<uint32_t>::max());
    for (uint32_t child_port = 0U; child_port < child_ports; ++child_port) {
        child_to_gamma[child_port] = gamma_index.indexFor(port_coords[child_port]);
    }

    for (uint32_t from = 0U; from < child_ports; ++from) {
        const uint32_t gamma_from = child_to_gamma[from];
        if (gamma_from == std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        const BitVector& row_bits = child_summary.row(from);
        const size_t num_words = row_bits.numWords();
        for (size_t word_idx = 0U; word_idx < num_words; ++word_idx) {
            uint64_t word = row_bits.word(word_idx);
            while (word != 0U) {
                const unsigned bit = static_cast<unsigned>(std::countr_zero(word));
                const uint32_t to = static_cast<uint32_t>(word_idx * 64U + bit);
                word &= word - 1U;
                if (to < child_ports) {
                    const uint32_t gamma_to = child_to_gamma[to];
                    if (gamma_to != std::numeric_limits<uint32_t>::max()) {
                        composed.set(gamma_from, gamma_to);
                    }
                }
            }
        }
    }
}

}  // namespace

GammaOrdering buildGammaOrdering(
    const TileSlot& parent_bbox,
    const std::span<const ChildBoundarySummary> children
) {
    std::vector<GridCoord> candidate_ports;
    for (const ChildBoundarySummary& child : children) {
        for (const GridCoord coord : child.port_coords) {
            if (parent_bbox.contains(coord)) {
                candidate_ports.push_back(coord);
            }
        }
    }

    const std::vector<GridCoord> canonical_exterior = canonicalBoundaryCoords(parent_bbox);
    struct CoordRank {
        uint64_t key = 0U;
        uint32_t rank = 0U;
    };
    std::vector<CoordRank> rank_table;
    rank_table.reserve(canonical_exterior.size());
    for (uint32_t r = 0U; r < canonical_exterior.size(); ++r) {
        rank_table.push_back(CoordRank{packCoord(canonical_exterior[r]), r});
    }
    std::sort(
        rank_table.begin(),
        rank_table.end(),
        [](const CoordRank& a, const CoordRank& b) noexcept {
            return a.key < b.key;
        }
    );

    const auto get_rank = [&](const GridCoord coord) noexcept -> uint32_t {
        const uint64_t key = packCoord(coord);
        const auto it = std::lower_bound(
            rank_table.begin(),
            rank_table.end(),
            key,
            [](const CoordRank& entry, const uint64_t probe) noexcept {
                return entry.key < probe;
            }
        );
        if (it != rank_table.end() && it->key == key) {
            return it->rank;
        }
        return std::numeric_limits<uint32_t>::max();
    };

    struct RankedPort {
        GridCoord coord;
        bool is_exterior = false;
        uint32_t rank = std::numeric_limits<uint32_t>::max();
    };

    std::vector<RankedPort> ranked_ports;
    ranked_ports.reserve(candidate_ports.size());
    for (const GridCoord coord : candidate_ports) {
        const bool is_ext = isOnTilePerimeter(coord, parent_bbox);
        const uint32_t rank = is_ext ? get_rank(coord) : std::numeric_limits<uint32_t>::max();
        ranked_ports.push_back(RankedPort{coord, is_ext, rank});
    }

    std::sort(
        ranked_ports.begin(),
        ranked_ports.end(),
        [](const RankedPort& lhs, const RankedPort& rhs) noexcept {
            if (lhs.is_exterior != rhs.is_exterior) {
                return lhs.is_exterior;
            }
            if (lhs.is_exterior && lhs.rank != rhs.rank) {
                return lhs.rank < rhs.rank;
            }
            if (lhs.coord.y != rhs.coord.y) {
                return lhs.coord.y < rhs.coord.y;
            }
            return lhs.coord.x < rhs.coord.x;
        }
    );

    GammaOrdering gamma;
    gamma.ports.reserve(ranked_ports.size());
    for (const RankedPort& rp : ranked_ports) {
        if (gamma.ports.empty() || !coordsEqual(gamma.ports.back(), rp.coord)) {
            gamma.ports.push_back(rp.coord);
        }
    }

    return gamma;
}

BitMatrix buildEmbedding(
    const std::span<const GridCoord> child_port_coords,
    const GammaOrdering& gamma
) {
    const GammaCoordIndex index(gamma);
    BitMatrix embedding(
        static_cast<uint32_t>(gamma.ports.size()),
        static_cast<uint32_t>(child_port_coords.size())
    );
    for (uint32_t child_port = 0U; child_port < child_port_coords.size(); ++child_port) {
        const uint32_t gamma_index = index.indexFor(child_port_coords[child_port]);
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
    const GammaCoordIndex index(gamma);
    const uint32_t gamma_size = static_cast<uint32_t>(gamma.ports.size());
    BitMatrix iface(gamma_size, gamma_size);
    if (gamma_size == 0U) {
        return iface;
    }

    uint32_t min_x = std::numeric_limits<uint32_t>::max();
    uint32_t max_x = 0U;
    uint32_t min_y = std::numeric_limits<uint32_t>::max();
    uint32_t max_y = 0U;
    for (const GridCoord p : gamma.ports) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }

    for (const SeamEdge& seam_edge : seam_edges) {
        const PortRecord& from_record = port_index.port(seam_edge.from_port_id);
        const PortRecord& to_record = port_index.port(seam_edge.to_port_id);
        if (from_record.tile_index == to_record.tile_index) {
            continue;
        }

        if (from_record.coord.x < min_x || from_record.coord.x > max_x ||
            from_record.coord.y < min_y || from_record.coord.y > max_y ||
            to_record.coord.x < min_x || to_record.coord.x > max_x ||
            to_record.coord.y < min_y || to_record.coord.y > max_y) {
            continue;
        }

        const uint32_t from_gamma = index.indexFor(from_record.coord);
        const uint32_t to_gamma = index.indexFor(to_record.coord);
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
    const BitMatrix& iface_adjacency
) {

    const uint32_t gamma_size = static_cast<uint32_t>(gamma.ports.size());
    BitMatrix composed(gamma_size, gamma_size);
    for (uint32_t index = 0U; index < gamma_size; ++index) {
        composed.set(index, index);
    }

    const GammaCoordIndex gamma_index(gamma);
    for (std::size_t child_index = 0U; child_index < children.size(); ++child_index) {
        const ChildBoundarySummary& child = children[child_index];
        if (child.boundary_summary == nullptr) {
            throw std::invalid_argument("composeInterfaceAdjacency: missing child summary");
        }

        scatterChildSummaryIntoGamma(
            composed,
            gamma_index,
            child.port_coords,
            *child.boundary_summary
        );
    }

    booleanOrInPlace(composed, iface_adjacency);
    return composed;
}

BitMatrix computeInterfaceClosure(
    BitMatrix composed_adjacency,
    BitMatrix* scratch,
    const ClosureDiagnosticContext* diag_ctx
) {
    const uint32_t num_vertices = composed_adjacency.numRows();
    const uint32_t squaring_count = (num_vertices <= 1U)
        ? 0U
        : static_cast<uint32_t>(std::bit_width(num_vertices - 1U));

    static const bool trace_enabled = []() {
        const char* env = std::getenv("HBRICK_TRACE_CLOSURE");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();

    if (trace_enabled) {
        uint32_t actual_rounds = 0U;
        bool reached_fixpoint = false;
        const auto start_time = std::chrono::steady_clock::now();
        BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            composed_adjacency,
            squaring_count,
            scratch,
            {},
            &actual_rounds,
            &reached_fixpoint
        );
        const auto end_time = std::chrono::steady_clock::now();
        const double time_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();

        const uint32_t parent_w = diag_ctx != nullptr ? diag_ctx->parent_w : 0U;
        const uint32_t parent_h = diag_ctx != nullptr ? diag_ctx->parent_h : 0U;
        const uint32_t children = diag_ctx != nullptr ? diag_ctx->children : 0U;

        std::fprintf(
            stderr,
            "HBRICK_CLOSURE\n"
            "    gamma=%u\n"
            "    parent_w=%u\n"
            "    parent_h=%u\n"
            "    children=%u\n"
            "    max_rounds=%u\n"
            "    actual_rounds=%u\n"
            "    early_fixpoint=%d\n"
            "    time_ms=%.3f\n",
            num_vertices,
            parent_w,
            parent_h,
            children,
            squaring_count,
            actual_rounds,
            reached_fixpoint ? 1 : 0,
            time_ms
        );
    } else {
        BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            composed_adjacency,
            squaring_count,
            scratch
        );
    }
    return composed_adjacency;
}

namespace {
PairSeedRecordCallback g_pair_seed_callback = nullptr;
}  // namespace

void setPairSeedRecordCallback(PairSeedRecordCallback callback) {
    g_pair_seed_callback = callback;
}

// EXPERIMENTAL C18:
// Pair-seeded interface closure prototype.
// This is not part of the canonical H-BRICK preprocessing path.
BitMatrix computeInterfaceClosurePairSeedExperimental(
    const TileSlot& parent_bbox,
    std::span<const ChildBoundarySummary> active_children,
    const PortIndex& port_index,
    std::span<const SeamEdge> seam_edges,
    const GammaOrdering& gamma,
    BitMatrix composed_adjacency,
    BitMatrix* scratch,
    PairSeedDiagnostics* diagnostics
) {
    (void)parent_bbox;
    const uint32_t gamma_size = static_cast<uint32_t>(gamma.ports.size());
    if (gamma_size <= 1U || active_children.empty()) {
        if (diagnostics != nullptr) {
            diagnostics->reached_fixpoint = true;
        }
        return composed_adjacency;
    }

    // 1. Identify geometrically adjacent child pairs
    struct AdjacentPair {
        uint32_t child_i = 0U;
        uint32_t child_j = 0U;
    };
    std::vector<AdjacentPair> adjacent_pairs;
    const uint32_t num_children = static_cast<uint32_t>(active_children.size());
    for (uint32_t i = 0U; i < num_children; ++i) {
        const TileSlot& slot_i = active_children[i].slot;
        for (uint32_t j = i + 1U; j < num_children; ++j) {
            const TileSlot& slot_j = active_children[j].slot;
            const bool h_adj =
                (slot_i.maxX() == slot_j.origin.x || slot_j.maxX() == slot_i.origin.x) &&
                (std::max(slot_i.origin.y, slot_j.origin.y) < std::min(slot_i.maxY(), slot_j.maxY()));
            const bool v_adj =
                (slot_i.maxY() == slot_j.origin.y || slot_j.maxY() == slot_i.origin.y) &&
                (std::max(slot_i.origin.x, slot_j.origin.x) < std::min(slot_i.maxX(), slot_j.maxX()));
            if (h_adj || v_adj) {
                adjacent_pairs.push_back({i, j});
            }
        }
    }

    const GammaCoordIndex gamma_index(gamma);

    std::vector<std::vector<uint32_t>> child_port_to_gamma(num_children);
    std::vector<uint32_t> gamma_to_child(gamma_size, std::numeric_limits<uint32_t>::max());
    std::vector<uint32_t> gamma_to_local_port(gamma_size, std::numeric_limits<uint32_t>::max());

    for (uint32_t k = 0U; k < num_children; ++k) {
        const auto& child = active_children[k];
        child_port_to_gamma[k].resize(child.port_coords.size(), std::numeric_limits<uint32_t>::max());
        for (uint32_t p = 0U; p < child.port_coords.size(); ++p) {
            const uint32_t g = gamma_index.indexFor(child.port_coords[p]);
            child_port_to_gamma[k][p] = g;
            if (g != std::numeric_limits<uint32_t>::max() && g < gamma_size) {
                gamma_to_child[g] = k;
                gamma_to_local_port[g] = p;
            }
        }
    }

    std::vector<int> pair_lookup(static_cast<size_t>(num_children) * num_children, -1);
    for (uint32_t p = 0U; p < adjacent_pairs.size(); ++p) {
        const uint32_t ci = adjacent_pairs[p].child_i;
        const uint32_t cj = adjacent_pairs[p].child_j;
        pair_lookup[static_cast<size_t>(ci) * num_children + cj] = static_cast<int>(p);
        pair_lookup[static_cast<size_t>(cj) * num_children + ci] = static_cast<int>(p);
    }

    struct PairEdge {
        uint32_t from_local = 0U;
        uint32_t to_local = 0U;
    };
    std::vector<std::vector<PairEdge>> pair_seams(adjacent_pairs.size());

    for (const SeamEdge& edge : seam_edges) {
        if (edge.from_port_id >= port_index.numPorts() || edge.to_port_id >= port_index.numPorts()) {
            continue;
        }
        const PortRecord& from_rec = port_index.port(edge.from_port_id);
        const PortRecord& to_rec = port_index.port(edge.to_port_id);
        if (from_rec.tile_index == to_rec.tile_index) {
            continue;
        }
        const uint32_t from_gamma = gamma_index.indexFor(from_rec.coord);
        const uint32_t to_gamma = gamma_index.indexFor(to_rec.coord);
        if (from_gamma == std::numeric_limits<uint32_t>::max() ||
            to_gamma == std::numeric_limits<uint32_t>::max() ||
            from_gamma >= gamma_size || to_gamma >= gamma_size) {
            continue;
        }
        const uint32_t c_from = gamma_to_child[from_gamma];
        const uint32_t c_to = gamma_to_child[to_gamma];
        if (c_from == std::numeric_limits<uint32_t>::max() ||
            c_to == std::numeric_limits<uint32_t>::max() ||
            c_from == c_to) {
            continue;
        }
        const int p_idx = pair_lookup[static_cast<size_t>(c_from) * num_children + c_to];
        if (p_idx >= 0) {
            const uint32_t ci = adjacent_pairs[static_cast<size_t>(p_idx)].child_i;
            const uint32_t p_i = static_cast<uint32_t>(active_children[ci].port_coords.size());
            uint32_t local_from = 0U;
            uint32_t local_to = 0U;
            if (c_from == ci) {
                local_from = gamma_to_local_port[from_gamma];
                local_to = p_i + gamma_to_local_port[to_gamma];
            } else {
                local_from = p_i + gamma_to_local_port[from_gamma];
                local_to = gamma_to_local_port[to_gamma];
            }
            pair_seams[static_cast<size_t>(p_idx)].push_back({local_from, local_to});
        }
    }

    // 2. Local pair closures
    const auto preclose_start = std::chrono::steady_clock::now();

    std::vector<BitMatrix> closed_pairs;
    closed_pairs.reserve(adjacent_pairs.size());

    BitMatrix pair_scratch;
    uint32_t total_pair_rounds = 0U;
    std::vector<uint32_t> pair_gammas;
    pair_gammas.reserve(adjacent_pairs.size());

    for (uint32_t p = 0U; p < adjacent_pairs.size(); ++p) {
        const uint32_t ci = adjacent_pairs[p].child_i;
        const uint32_t cj = adjacent_pairs[p].child_j;
        const uint32_t p_i = static_cast<uint32_t>(active_children[ci].port_coords.size());
        const uint32_t p_j = static_cast<uint32_t>(active_children[cj].port_coords.size());
        const uint32_t d = p_i + p_j;
        pair_gammas.push_back(d);

        BitMatrix pair_mat(d, d);
        for (uint32_t k = 0U; k < d; ++k) {
            pair_mat.set(k, k);
        }

        if (active_children[ci].boundary_summary != nullptr) {
            const BitMatrix& s_i = *active_children[ci].boundary_summary;
            for (uint32_t r = 0U; r < p_i; ++r) {
                const BitVector& row_bits = s_i.row(r);
                for (size_t w = 0U; w < row_bits.numWords(); ++w) {
                    uint64_t word = row_bits.word(w);
                    while (word != 0U) {
                        const unsigned bit = static_cast<unsigned>(std::countr_zero(word));
                        const uint32_t c = static_cast<uint32_t>(w * 64U + bit);
                        word &= word - 1U;
                        if (c < p_i) {
                            pair_mat.set(r, c);
                        }
                    }
                }
            }
        }

        if (active_children[cj].boundary_summary != nullptr) {
            const BitMatrix& s_j = *active_children[cj].boundary_summary;
            for (uint32_t r = 0U; r < p_j; ++r) {
                const BitVector& row_bits = s_j.row(r);
                for (size_t w = 0U; w < row_bits.numWords(); ++w) {
                    uint64_t word = row_bits.word(w);
                    while (word != 0U) {
                        const unsigned bit = static_cast<unsigned>(std::countr_zero(word));
                        const uint32_t c = static_cast<uint32_t>(w * 64U + bit);
                        word &= word - 1U;
                        if (c < p_j) {
                            pair_mat.set(p_i + r, p_i + c);
                        }
                    }
                }
            }
        }

        for (const auto& pe : pair_seams[p]) {
            if (pe.from_local < d && pe.to_local < d) {
                pair_mat.set(pe.from_local, pe.to_local);
            }
        }

        const uint32_t pair_squarings = (d <= 1U)
            ? 0U
            : static_cast<uint32_t>(std::bit_width(d - 1U));

        uint32_t pair_actual = 0U;
        bool pair_fp = false;
        BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            pair_mat,
            pair_squarings,
            &pair_scratch,
            {},
            &pair_actual,
            &pair_fp
        );
        total_pair_rounds += pair_actual;
        closed_pairs.push_back(std::move(pair_mat));
    }

    const auto preclose_end = std::chrono::steady_clock::now();
    const double pair_preclose_ms =
        std::chrono::duration<double, std::milli>(preclose_end - preclose_start).count();

    // 3. Scatter into global relation
    const auto scatter_start = std::chrono::steady_clock::now();

    BitMatrix seeded_global = std::move(composed_adjacency);

    for (uint32_t p = 0U; p < adjacent_pairs.size(); ++p) {
        const uint32_t ci = adjacent_pairs[p].child_i;
        const uint32_t cj = adjacent_pairs[p].child_j;
        const uint32_t p_i = static_cast<uint32_t>(active_children[ci].port_coords.size());
        const uint32_t p_j = static_cast<uint32_t>(active_children[cj].port_coords.size());
        const uint32_t d = p_i + p_j;
        const BitMatrix& c_ij = closed_pairs[p];

        std::vector<uint32_t> pair_to_gamma(d);
        for (uint32_t u = 0U; u < p_i; ++u) {
            pair_to_gamma[u] = child_port_to_gamma[ci][u];
        }
        for (uint32_t v = 0U; v < p_j; ++v) {
            pair_to_gamma[p_i + v] = child_port_to_gamma[cj][v];
        }

        for (uint32_t r = 0U; r < d; ++r) {
            const uint32_t g_r = pair_to_gamma[r];
            if (g_r == std::numeric_limits<uint32_t>::max() || g_r >= gamma_size) {
                continue;
            }
            const BitVector& row_bits = c_ij.row(r);
            for (size_t w = 0U; w < row_bits.numWords(); ++w) {
                uint64_t word = row_bits.word(w);
                while (word != 0U) {
                    const unsigned bit = static_cast<unsigned>(std::countr_zero(word));
                    const uint32_t c = static_cast<uint32_t>(w * 64U + bit);
                    word &= word - 1U;
                    if (c < d) {
                        const uint32_t g_c = pair_to_gamma[c];
                        if (g_c != std::numeric_limits<uint32_t>::max() && g_c < gamma_size) {
                            seeded_global.set(g_r, g_c);
                        }
                    }
                }
            }
        }
    }

    const auto scatter_end = std::chrono::steady_clock::now();
    const double pair_scatter_ms =
        std::chrono::duration<double, std::milli>(scatter_end - scatter_start).count();

    // 4. Final global closure
    const uint32_t global_squaring_count = (gamma_size <= 1U)
        ? 0U
        : static_cast<uint32_t>(std::bit_width(gamma_size - 1U));

    uint32_t seeded_actual_rounds = 0U;
    bool seeded_fixpoint = false;

    const auto global_start = std::chrono::steady_clock::now();

    BooleanClosure::transitiveClosureKleeneSquaringInPlace(
        seeded_global,
        global_squaring_count,
        scratch,
        {},
        &seeded_actual_rounds,
        &seeded_fixpoint
    );

    const auto global_end = std::chrono::steady_clock::now();
    const double seeded_global_ms =
        std::chrono::duration<double, std::milli>(global_end - global_start).count();

    if (diagnostics != nullptr) {
        diagnostics->pair_count = static_cast<uint32_t>(adjacent_pairs.size());
        diagnostics->pair_total_rounds = total_pair_rounds;
        diagnostics->pair_preclosure_ms = pair_preclose_ms;
        diagnostics->pair_scatter_ms = pair_scatter_ms;
        diagnostics->seeded_global_max_rounds = global_squaring_count;
        diagnostics->seeded_global_actual_rounds = seeded_actual_rounds;
        diagnostics->reached_fixpoint = seeded_fixpoint;
        diagnostics->seeded_global_closure_ms = seeded_global_ms;
        diagnostics->pairseed_total_ms =
            pair_preclose_ms + pair_scatter_ms + seeded_global_ms;
        diagnostics->density_after_bits = seeded_global.popcount();

        if (!pair_gammas.empty()) {
            std::sort(pair_gammas.begin(), pair_gammas.end());
            diagnostics->min_pair_gamma = pair_gammas.front();
            diagnostics->max_pair_gamma = pair_gammas.back();
            diagnostics->median_pair_gamma = pair_gammas[pair_gammas.size() / 2U];
            uint32_t sum_p = 0U;
            for (uint32_t pg : pair_gammas) {
                sum_p += pg;
            }
            diagnostics->pair_total_ports = sum_p;
        }
    }

    return seeded_global;
}


BitMatrix projectExternalSummary(
    const TileSlot& parent_bbox,
    const GammaOrdering& gamma,
    const BitMatrix& interface_closure
) {
    const GammaCoordIndex index(gamma);
    const std::vector<GridCoord> exterior_ports = collectExteriorPorts(parent_bbox, gamma);
    const uint32_t num_ext = static_cast<uint32_t>(exterior_ports.size());
    BitMatrix boundary(num_ext, num_ext);
    if (num_ext == 0U) {
        return boundary;
    }

    std::vector<uint32_t> ext_to_gamma(num_ext);
    for (uint32_t i = 0U; i < num_ext; ++i) {
        ext_to_gamma[i] = index.indexFor(exterior_ports[i]);
    }

    for (uint32_t from = 0U; from < num_ext; ++from) {
        const uint32_t gamma_from = ext_to_gamma[from];
        if (gamma_from == std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        const BitVector& row = interface_closure.row(gamma_from);
        BitVector& out_row = boundary.row(from);
        for (uint32_t to = 0U; to < num_ext; ++to) {
            const uint32_t gamma_to = ext_to_gamma[to];
            if (gamma_to != std::numeric_limits<uint32_t>::max() && row.test(gamma_to)) {
                out_row.set(to);
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
    const uint64_t max_memory_bytes,
    PreprocessMemoryLedger* ledger,
    BitMatrix* closure_scratch
) {
    SuperTileSummary result{};
    result.slot = parent_bbox;
    const uint64_t charged_before =
        ledger != nullptr ? ledger->chargedBytes() : 0U;

    auto failPolicy = [&]() -> SuperTileSummary {
        SuperTileSummary skipped{};
        skipped.slot = parent_bbox;
        skipped.status = BaselineStatus::SkippedByPolicy;
        return skipped;
    };

    auto failOom = [&]() -> SuperTileSummary {
        if (ledger != nullptr) {
            ledger->releaseCharge(ledger->chargedBytes() - charged_before);
        }
        SuperTileSummary failed{};
        failed.slot = parent_bbox;
        failed.status = BaselineStatus::OutOfMemory;
        return failed;
    };

    std::vector<ChildBoundarySummary> active_children;
    active_children.reserve(children.size());
    for (const ChildBoundarySummary& child : children) {
        if (child.boundary_summary == nullptr || child.port_coords.empty()) {
            continue;
        }
        active_children.push_back(child);
    }

    if (active_children.empty()) {
        return failPolicy();
    }

    result.gamma = buildGammaOrdering(parent_bbox, active_children);
    if (result.gamma.ports.empty()) {
        return failPolicy();
    }

    const uint32_t gamma_size = static_cast<uint32_t>(result.gamma.ports.size());

    // Preflight retained matrices only (interface_closure + transpose +
    // boundary_summary). Temporary matrices are freed at function exit.
    // Embeddings are charged inline below as they are retained.
    const uint64_t one_gamma_matrix =
        exactBitMatrixStorageBytes(gamma_size, gamma_size);
    result.exterior_ports =
        collectExteriorPorts(parent_bbox, result.gamma);
    const uint64_t exterior_matrix =
        exactBitMatrixStorageBytes(
            static_cast<uint32_t>(result.exterior_ports.size()),
            static_cast<uint32_t>(result.exterior_ports.size())
        );
    const uint64_t retained_bytes =
        one_gamma_matrix * 2U + exterior_matrix;

    uint64_t remaining_bytes = kHBrickUnlimitedMemoryBytes;
    if (ledger != nullptr && !ledger->isUnlimited()) {
        remaining_bytes = ledger->capBytes() - ledger->chargedBytes();
    } else if (!isUnlimitedMemoryBudget(max_memory_bytes)) {
        remaining_bytes = max_memory_bytes;
    }
    if (retained_bytes > remaining_bytes) {
        return failOom();
    }

    const GammaCoordIndex gamma_index(result.gamma);

    result.child_embedding_of.assign(children.size(), UINT32_MAX);
    result.child_port_to_gamma.reserve(active_children.size());
    uint32_t active_index = 0U;
    for (uint32_t child_slot = 0U; child_slot < children.size(); ++child_slot) {
        const ChildBoundarySummary& child = children[child_slot];
        if (child.boundary_summary == nullptr || child.port_coords.empty()) {
            continue;
        }
        result.child_embedding_of[child_slot] = active_index;
        ++active_index;

        std::vector<uint32_t> port_to_gamma(
            child.port_coords.size(),
            std::numeric_limits<uint32_t>::max()
        );
        for (uint32_t child_port = 0U; child_port < child.port_coords.size(); ++child_port) {
            const uint32_t mapped_gamma =
                gamma_index.indexFor(child.port_coords[child_port]);
            if (mapped_gamma != std::numeric_limits<uint32_t>::max()) {
                port_to_gamma[child_port] = mapped_gamma;
            }
        }
        if (ledger != nullptr
            && !ledger->tryCharge(static_cast<uint64_t>(port_to_gamma.capacity()) * sizeof(uint32_t))) {
            return failOom();
        }
        result.child_port_to_gamma.push_back(std::move(port_to_gamma));
    }

    const BitMatrix iface_adjacency =
        buildIfaceAdjacency(result.gamma, port_index, seam_edges);
    BitMatrix composed = composeInterfaceAdjacency(
        result.gamma,
        active_children,
        iface_adjacency
    );
    static const bool pair_seed_enabled = []() {
        const char* env = std::getenv("HBRICK_EXPERIMENT_PAIR_SEED");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();

    if (pair_seed_enabled) {
        BitMatrix composed_copy = composed;
        const auto baseline_start = std::chrono::steady_clock::now();
        uint32_t baseline_actual_rounds = 0U;
        bool baseline_fixpoint = false;
        const uint32_t num_vertices = composed_copy.numRows();
        const uint32_t baseline_max_rounds = (num_vertices <= 1U)
            ? 0U
            : static_cast<uint32_t>(std::bit_width(num_vertices - 1U));
        BooleanClosure::transitiveClosureKleeneSquaringInPlace(
            composed_copy,
            baseline_max_rounds,
            closure_scratch,
            {},
            &baseline_actual_rounds,
            &baseline_fixpoint
        );
        const auto baseline_end = std::chrono::steady_clock::now();
        const double baseline_ms =
            std::chrono::duration<double, std::milli>(baseline_end - baseline_start).count();

        PairSeedDiagnostics diag{};
        diag.baseline_max_rounds = baseline_max_rounds;
        diag.baseline_actual_rounds = baseline_actual_rounds;
        diag.baseline_closure_ms = baseline_ms;
        diag.density_before_bits = composed.popcount();

        BitMatrix pairseed_closure = computeInterfaceClosurePairSeedExperimental(
            parent_bbox,
            active_children,
            port_index,
            seam_edges,
            result.gamma,
            std::move(composed),
            closure_scratch,
            &diag
        );

        const bool equal = bitMatricesEqual(composed_copy, pairseed_closure);
        diag.closure_equal = equal;
        diag.rounds_saved = static_cast<int32_t>(baseline_actual_rounds) -
                            static_cast<int32_t>(diag.seeded_global_actual_rounds);
        diag.speedup = (diag.pairseed_total_ms > 0.0)
            ? (baseline_ms / diag.pairseed_total_ms)
            : 1.0;

        if (!equal) {
            uint32_t diff_r = 0U;
            uint32_t diff_c = 0U;
            bool found_diff = false;
            for (uint32_t r = 0U; r < num_vertices && !found_diff; ++r) {
                for (uint32_t c = 0U; c < num_vertices && !found_diff; ++c) {
                    if (composed_copy.test(r, c) != pairseed_closure.test(r, c)) {
                        diff_r = r;
                        diff_c = c;
                        found_diff = true;
                    }
                }
            }
            std::fprintf(
                stderr,
                "C18 ERROR: MISMATCH between baseline and pairseed closure!\n"
                "  parent_w=%u parent_h=%u gamma=%u children=%u pairs=%u\n"
                "  First mismatch at (row=%u, col=%u): baseline=%d, pairseed=%d\n",
                parent_bbox.extent.width,
                parent_bbox.extent.height,
                num_vertices,

                static_cast<uint32_t>(active_children.size()),
                diag.pair_count,
                diff_r,
                diff_c,
                composed_copy.test(diff_r, diff_c) ? 1 : 0,
                pairseed_closure.test(diff_r, diff_c) ? 1 : 0
            );
            std::abort();
        }

        static const bool trace_pairseed = []() {
            const char* env = std::getenv("HBRICK_TRACE_PAIRSEED");
            return env != nullptr && env[0] != '\0' && env[0] != '0';
        }();

        if (trace_pairseed) {
            std::fprintf(
                stderr,
                "HBRICK_PAIRSEED\n"
                "    parent_w=%u\n"
                "    parent_h=%u\n"
                "    children=%u\n"
                "    gamma=%u\n"
                "    pair_count=%u\n"
                "    min_pair_gamma=%u\n"
                "    median_pair_gamma=%u\n"
                "    max_pair_gamma=%u\n"
                "    baseline_rounds=%u\n"
                "    baseline_ms=%.3f\n"
                "    pair_rounds_total=%u\n"
                "    pair_preclosure_ms=%.3f\n"
                "    pair_scatter_ms=%.3f\n"
                "    seeded_global_rounds=%u\n"
                "    seeded_global_ms=%.3f\n"
                "    pairseed_total_ms=%.3f\n"
                "    rounds_saved=%d\n"
                "    speedup=%.3f\n"
                "    closure_equal=%d\n"
                "    density_before_bits=%llu\n"
                "    density_after_bits=%llu\n",
                parent_bbox.extent.width,
                parent_bbox.extent.height,
                static_cast<uint32_t>(active_children.size()),
                num_vertices,
                diag.pair_count,
                diag.min_pair_gamma,
                diag.median_pair_gamma,
                diag.max_pair_gamma,
                diag.baseline_actual_rounds,
                diag.baseline_closure_ms,
                diag.pair_total_rounds,
                diag.pair_preclosure_ms,
                diag.pair_scatter_ms,
                diag.seeded_global_actual_rounds,
                diag.seeded_global_closure_ms,
                diag.pairseed_total_ms,
                diag.rounds_saved,
                diag.speedup,
                equal ? 1 : 0,
                static_cast<unsigned long long>(diag.density_before_bits),
                static_cast<unsigned long long>(diag.density_after_bits)
            );
        }

        if (g_pair_seed_callback != nullptr) {
            const PairSeedClosureRecord record{
                .parent_w = parent_bbox.extent.width,
                .parent_h = parent_bbox.extent.height,
                .children = static_cast<uint32_t>(active_children.size()),
                .gamma = num_vertices,
                .pair_count = diag.pair_count,
                .min_pair_gamma = diag.min_pair_gamma,
                .median_pair_gamma = diag.median_pair_gamma,
                .max_pair_gamma = diag.max_pair_gamma,
                .baseline_rounds = diag.baseline_actual_rounds,
                .baseline_ms = diag.baseline_closure_ms,
                .pair_rounds_total = diag.pair_total_rounds,
                .pair_preclosure_ms = diag.pair_preclosure_ms,
                .pair_scatter_ms = diag.pair_scatter_ms,
                .seeded_global_rounds = diag.seeded_global_actual_rounds,
                .seeded_global_ms = diag.seeded_global_closure_ms,
                .pairseed_total_ms = diag.pairseed_total_ms,
                .rounds_saved = diag.rounds_saved,
                .speedup = diag.speedup,
                .closure_equal = equal,
                .density_before_bits = diag.density_before_bits,
                .density_after_bits = diag.density_after_bits
            };
            g_pair_seed_callback(record);
        }

        result.interface_closure = std::move(pairseed_closure);
    } else {
        const ClosureDiagnosticContext diag_ctx{
            .parent_w = parent_bbox.extent.width,
            .parent_h = parent_bbox.extent.height,
            .children = static_cast<uint32_t>(active_children.size())
        };
        result.interface_closure = computeInterfaceClosure(
            std::move(composed),
            closure_scratch,
            &diag_ctx
        );
    }

    if (ledger != nullptr
        && !ledger->tryCharge(result.interface_closure.memoryBytes())) {
        return failOom();
    }
    result.interface_closure_transpose = BitMatrix(
        result.interface_closure.numCols(),
        result.interface_closure.numRows()
    );
    if (ledger != nullptr
        && !ledger->tryCharge(result.interface_closure_transpose.memoryBytes())) {
        return failOom();
    }
    const uint32_t closure_rows = result.interface_closure.numRows();
    const uint32_t closure_cols = result.interface_closure.numCols();
    for (uint32_t row = 0U; row < closure_rows; ++row) {
        const BitVector& row_vec = result.interface_closure.row(row);
        const size_t num_words = row_vec.numWords();
        for (size_t w = 0U; w < num_words; ++w) {
            uint64_t bits = row_vec.word(w);
            while (bits != 0U) {
                const unsigned bit = static_cast<unsigned>(std::countr_zero(bits));
                const uint32_t col = static_cast<uint32_t>(w * 64U + bit);
                bits &= bits - 1U;
                if (col < closure_cols) {
                    result.interface_closure_transpose.set(col, row);
                }
            }
        }
    }
    result.boundary_summary =
        projectExternalSummary(parent_bbox, result.gamma, result.interface_closure);
    if (ledger != nullptr
        && !ledger->tryCharge(result.boundary_summary.memoryBytes())) {
        return failOom();
    }

    result.exterior_gamma_indices.reserve(result.exterior_ports.size());
    for (const GridCoord coord : result.exterior_ports) {
        result.exterior_gamma_indices.push_back(gamma_index.indexFor(coord));
    }

    if (ledger != nullptr) {
        const uint64_t extra_vectors_bytes =
            static_cast<uint64_t>(result.child_embedding_of.capacity()) * sizeof(uint32_t)
            + static_cast<uint64_t>(result.exterior_ports.capacity()) * sizeof(GridCoord)
            + static_cast<uint64_t>(result.exterior_gamma_indices.capacity()) * sizeof(uint32_t)
            + static_cast<uint64_t>(result.gamma.ports.capacity()) * sizeof(GridCoord)
            + static_cast<uint64_t>(result.child_port_to_gamma.capacity()) * sizeof(std::vector<uint32_t>);
        if (!ledger->tryCharge(extra_vectors_bytes)) {
            return failOom();
        }
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
    // Design: expose exterior boundary summary S_U to the parent, not the full
    // interface closure S̄_U / Γ_U. This is the hierarchy compression step.
    port_coord_storage = summary.exterior_ports;

    ChildBoundarySummary child{};
    child.slot = summary.slot;
    child.port_coords = port_coord_storage;
    child.boundary_summary = &summary.boundary_summary;
    child.tile_index = std::numeric_limits<uint32_t>::max();
    return child;
}

}  // namespace hbrick
