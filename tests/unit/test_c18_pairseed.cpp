// EXPERIMENTAL C18:
// Unit tests for pair-seeded super-tile interface closure prototype.
// This is not part of the canonical H-BRICK preprocessing path.

#include <gtest/gtest.h>

#include <cstdlib>
#include <random>
#include <vector>

#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/core/grid_coord.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/port_index.hpp"
#include "hbrick/tile/seam_edge.hpp"
#include "hbrick/tile/super_tile_composer.hpp"
#include "hbrick/tile/tile_size.hpp"
#include "hbrick/tile/tile_slot.hpp"

namespace {

hbrick::TileSlot parentSlotFromRegion(const hbrick::RegionNode& region) {
    hbrick::TileSlot slot{};
    slot.tile_i = region.region_i;
    slot.tile_j = region.region_j;
    slot.origin = region.origin;
    slot.extent = region.extent;
    return slot;
}

}  // namespace

// Test 1: Hand-constructed small case with two adjacent children and seams
TEST(C18PairSeed, SmallHandConstructedTwoChildrenMatchesBaseline) {
    // Parent 8x4 covering two 4x4 children side-by-side
    hbrick::TileSlot parent{};
    parent.origin = {0U, 0U};
    parent.extent = {8U, 4U};

    hbrick::TileSlot child_a_slot{};
    child_a_slot.origin = {0U, 0U};
    child_a_slot.extent = {4U, 4U};

    hbrick::TileSlot child_b_slot{};
    child_b_slot.origin = {4U, 0U};
    child_b_slot.extent = {4U, 4U};

    // Child A has 2 ports: (3, 1) and (0, 1)
    const std::vector<hbrick::GridCoord> a_ports = {{3U, 1U}, {0U, 1U}};
    // Child B has 2 ports: (4, 1) and (7, 1)
    const std::vector<hbrick::GridCoord> b_ports = {{4U, 1U}, {7U, 1U}};

    // S_A: port 1 reaches port 0 (from (0,1) to (3,1))
    hbrick::BitMatrix s_a(2U, 2U);
    s_a.set(0U, 0U);
    s_a.set(1U, 1U);
    s_a.set(1U, 0U);

    // S_B: port 0 reaches port 1 (from (4,1) to (7,1))
    hbrick::BitMatrix s_b(2U, 2U);
    s_b.set(0U, 0U);
    s_b.set(1U, 1U);
    s_b.set(0U, 1U);

    const hbrick::ChildBoundarySummary child_a{
        .slot = child_a_slot,
        .port_coords = a_ports,
        .boundary_summary = &s_a,
        .tile_index = 0U,
    };
    const hbrick::ChildBoundarySummary child_b{
        .slot = child_b_slot,
        .port_coords = b_ports,
        .boundary_summary = &s_b,
        .tile_index = 1U,
    };
    const hbrick::ChildBoundarySummary children[] = {child_a, child_b};

    const hbrick::GammaOrdering gamma = hbrick::buildGammaOrdering(parent, children);
    ASSERT_EQ(gamma.ports.size(), 4U);

    // Build dummy PortIndex for seam edge lookup
    // Seam edge from A's (3,1) to B's (4,1)
    // We construct a mock PortIndex using a small grid graph
    hbrick::MazeLayout layout(8U, 4U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::BrickIndex brick_index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max(),
        true
    );
    ASSERT_EQ(brick_index.status(), hbrick::BaselineStatus::Completed);

    // Find port IDs in brick_index for (3,1) and (4,1)
    uint32_t port_3_1 = std::numeric_limits<uint32_t>::max();
    uint32_t port_4_1 = std::numeric_limits<uint32_t>::max();
    for (uint32_t p = 0U; p < brick_index.ports().numPorts(); ++p) {
        const auto& rec = brick_index.ports().port(p);
        if (rec.coord.x == 3U && rec.coord.y == 1U) {
            port_3_1 = p;
        }
        if (rec.coord.x == 4U && rec.coord.y == 1U) {
            port_4_1 = p;
        }
    }
    ASSERT_NE(port_3_1, std::numeric_limits<uint32_t>::max());
    ASSERT_NE(port_4_1, std::numeric_limits<uint32_t>::max());

    std::vector<hbrick::SeamEdge> seams = {
        hbrick::SeamEdge{.from_port_id = port_3_1, .to_port_id = port_4_1}
    };

    const hbrick::BitMatrix iface_adj =
        hbrick::buildIfaceAdjacency(gamma, brick_index.ports(), seams);
    const hbrick::BitMatrix composed =
        hbrick::composeInterfaceAdjacency(gamma, children, iface_adj);

    hbrick::BitMatrix baseline_closure =
        hbrick::computeInterfaceClosure(composed);

    hbrick::PairSeedDiagnostics diag{};
    hbrick::BitMatrix pairseed_closure =
        hbrick::computeInterfaceClosurePairSeedExperimental(
            parent,
            children,
            brick_index.ports(),
            seams,
            gamma,
            composed,
            nullptr,
            &diag
        );

    const bool equal = hbrick::bitMatricesEqual(baseline_closure, pairseed_closure);
    EXPECT_TRUE(equal);
    EXPECT_EQ(diag.pair_count, 1U);
}

// Test 2: Directed one-way seam case
TEST(C18PairSeed, DirectedOneWaySeamPreservesDirectionality) {
    hbrick::TileSlot parent{};
    parent.origin = {0U, 0U};
    parent.extent = {8U, 4U};

    hbrick::TileSlot slot_a{.origin = {0U, 0U}, .extent = {4U, 4U}};
    hbrick::TileSlot slot_b{.origin = {4U, 0U}, .extent = {4U, 4U}};

    const std::vector<hbrick::GridCoord> a_ports = {{3U, 2U}};
    const std::vector<hbrick::GridCoord> b_ports = {{4U, 2U}};

    hbrick::BitMatrix s_a(1U, 1U);
    s_a.set(0U, 0U);
    hbrick::BitMatrix s_b(1U, 1U);
    s_b.set(0U, 0U);

    const hbrick::ChildBoundarySummary child_a{
        .slot = slot_a,
        .port_coords = a_ports,
        .boundary_summary = &s_a,
        .tile_index = 0U,
    };
    const hbrick::ChildBoundarySummary child_b{
        .slot = slot_b,
        .port_coords = b_ports,
        .boundary_summary = &s_b,
        .tile_index = 1U,
    };
    const hbrick::ChildBoundarySummary children[] = {child_a, child_b};

    const hbrick::GammaOrdering gamma = hbrick::buildGammaOrdering(parent, children);
    ASSERT_EQ(gamma.ports.size(), 2U);

    hbrick::MazeLayout layout(8U, 4U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::BrickIndex brick_index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max(),
        true
    );

    uint32_t p_a = std::numeric_limits<uint32_t>::max();
    uint32_t p_b = std::numeric_limits<uint32_t>::max();
    for (uint32_t p = 0U; p < brick_index.ports().numPorts(); ++p) {
        const auto& rec = brick_index.ports().port(p);
        if (rec.coord.x == 3U && rec.coord.y == 2U) {
            p_a = p;
        }
        if (rec.coord.x == 4U && rec.coord.y == 2U) {
            p_b = p;
        }
    }
    ASSERT_NE(p_a, std::numeric_limits<uint32_t>::max());
    ASSERT_NE(p_b, std::numeric_limits<uint32_t>::max());

    // One-way seam strictly from A to B
    std::vector<hbrick::SeamEdge> seams = {
        hbrick::SeamEdge{.from_port_id = p_a, .to_port_id = p_b}
    };

    const hbrick::BitMatrix iface_adj =
        hbrick::buildIfaceAdjacency(gamma, brick_index.ports(), seams);
    const hbrick::BitMatrix composed =
        hbrick::composeInterfaceAdjacency(gamma, children, iface_adj);

    hbrick::BitMatrix baseline_closure = hbrick::computeInterfaceClosure(composed);
    hbrick::PairSeedDiagnostics diag{};
    hbrick::BitMatrix pairseed_closure =
        hbrick::computeInterfaceClosurePairSeedExperimental(
            parent,
            children,
            brick_index.ports(),
            seams,
            gamma,
            composed,
            nullptr,
            &diag
        );

    EXPECT_TRUE(hbrick::bitMatricesEqual(baseline_closure, pairseed_closure));

    // Gamma index for (3,2) should reach (4,2), but (4,2) must NOT reach (3,2)
    auto find_in_gamma = [&](hbrick::GridCoord c) {
        for (uint32_t idx = 0U; idx < gamma.ports.size(); ++idx) {
            if (gamma.ports[idx].x == c.x && gamma.ports[idx].y == c.y) {
                return idx;
            }
        }
        return std::numeric_limits<uint32_t>::max();
    };
    const uint32_t g_a = find_in_gamma({3U, 2U});
    const uint32_t g_b = find_in_gamma({4U, 2U});
    ASSERT_NE(g_a, std::numeric_limits<uint32_t>::max());
    ASSERT_NE(g_b, std::numeric_limits<uint32_t>::max());
    EXPECT_TRUE(pairseed_closure.test(g_a, g_b));
    EXPECT_FALSE(pairseed_closure.test(g_b, g_a));

}

// Test 3: Path requiring alternating travel across more than one neighboring pair: A -> B -> C
TEST(C18PairSeed, PathAcrossMultipleNeighboringPairsMatchesBaseline) {
    hbrick::TileSlot parent{.origin = {0U, 0U}, .extent = {12U, 4U}};
    hbrick::TileSlot slot_a{.origin = {0U, 0U}, .extent = {4U, 4U}};
    hbrick::TileSlot slot_b{.origin = {4U, 0U}, .extent = {4U, 4U}};
    hbrick::TileSlot slot_c{.origin = {8U, 0U}, .extent = {4U, 4U}};

    const std::vector<hbrick::GridCoord> a_ports = {{0U, 1U}, {3U, 1U}};
    const std::vector<hbrick::GridCoord> b_ports = {{4U, 1U}, {7U, 1U}};
    const std::vector<hbrick::GridCoord> c_ports = {{8U, 1U}, {11U, 1U}};

    // S_A: (0,1) -> (3,1)
    hbrick::BitMatrix s_a(2U, 2U);
    s_a.set(0U, 0U); s_a.set(1U, 1U); s_a.set(0U, 1U);

    // S_B: (4,1) -> (7,1)
    hbrick::BitMatrix s_b(2U, 2U);
    s_b.set(0U, 0U); s_b.set(1U, 1U); s_b.set(0U, 1U);

    // S_C: (8,1) -> (11,1)
    hbrick::BitMatrix s_c(2U, 2U);
    s_c.set(0U, 0U); s_c.set(1U, 1U); s_c.set(0U, 1U);

    const hbrick::ChildBoundarySummary children[] = {
        {.slot = slot_a, .port_coords = a_ports, .boundary_summary = &s_a, .tile_index = 0U},
        {.slot = slot_b, .port_coords = b_ports, .boundary_summary = &s_b, .tile_index = 1U},
        {.slot = slot_c, .port_coords = c_ports, .boundary_summary = &s_c, .tile_index = 2U}
    };

    const hbrick::GammaOrdering gamma = hbrick::buildGammaOrdering(parent, children);

    hbrick::MazeLayout layout(12U, 4U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::BrickIndex brick_index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max(),
        true
    );

    auto get_pid = [&](uint32_t x, uint32_t y) {
        for (uint32_t p = 0U; p < brick_index.ports().numPorts(); ++p) {
            const auto& rec = brick_index.ports().port(p);
            if (rec.coord.x == x && rec.coord.y == y) return p;
        }
        return std::numeric_limits<uint32_t>::max();
    };

    std::vector<hbrick::SeamEdge> seams = {
        hbrick::SeamEdge{.from_port_id = get_pid(3U, 1U), .to_port_id = get_pid(4U, 1U)},
        hbrick::SeamEdge{.from_port_id = get_pid(7U, 1U), .to_port_id = get_pid(8U, 1U)}
    };

    const hbrick::BitMatrix iface_adj =
        hbrick::buildIfaceAdjacency(gamma, brick_index.ports(), seams);
    const hbrick::BitMatrix composed =
        hbrick::composeInterfaceAdjacency(gamma, children, iface_adj);

    hbrick::BitMatrix baseline_closure = hbrick::computeInterfaceClosure(composed);
    hbrick::PairSeedDiagnostics diag{};
    hbrick::BitMatrix pairseed_closure =
        hbrick::computeInterfaceClosurePairSeedExperimental(
            parent,
            children,
            brick_index.ports(),
            seams,
            gamma,
            composed,
            nullptr,
            &diag
        );

    EXPECT_TRUE(hbrick::bitMatricesEqual(baseline_closure, pairseed_closure));

    // (0,1) in A must reach (11,1) in C through the multi-pair path!
    auto find_in_gamma = [&](hbrick::GridCoord c) {
        for (uint32_t idx = 0U; idx < gamma.ports.size(); ++idx) {
            if (gamma.ports[idx].x == c.x && gamma.ports[idx].y == c.y) {
                return idx;
            }
        }
        return std::numeric_limits<uint32_t>::max();
    };
    const uint32_t g_start = find_in_gamma({0U, 1U});
    const uint32_t g_end = find_in_gamma({11U, 1U});
    ASSERT_NE(g_start, std::numeric_limits<uint32_t>::max());
    ASSERT_NE(g_end, std::numeric_limits<uint32_t>::max());
    EXPECT_TRUE(pairseed_closure.test(g_start, g_end));
    // 2 adjacent pairs: (A, B) and (B, C)
    EXPECT_EQ(diag.pair_count, 2U);
}


// Test 4: PairSeed disabled -> existing baseline behavior unchanged
TEST(C18PairSeed, DisabledLeavesDefaultBehaviorUnchanged) {
    // Make sure HBRICK_EXPERIMENT_PAIR_SEED is unset
    unsetenv("HBRICK_EXPERIMENT_PAIR_SEED");

    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::BrickIndex brick_index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max(),
        true
    );
    const hbrick::TileDecomposition decomposition = brick_index.tiles().decomposition();
    const hbrick::HierarchyTree tree = hbrick::HierarchyTree::build(
        decomposition,
        hbrick::GroupSize{2U, 2U},
        2U
    );
    const hbrick::RegionNode& parent_region = tree.node(1U, 0U);
    const hbrick::TileSlot parent_slot = parentSlotFromRegion(parent_region);

    std::vector<hbrick::ChildBoundarySummary> children;
    std::vector<std::vector<hbrick::GridCoord>> child_port_storage;
    for (const hbrick::RegionNodeId& child_id : parent_region.children) {
        child_port_storage.emplace_back();
        const hbrick::BaseTileSummary& summary =
            brick_index.tiles().summaryByIndex(child_id.index);
        children.push_back(hbrick::childBoundaryFromBaseTile(
            summary,
            child_id.index,
            child_port_storage.back()
        ));
    }

    const hbrick::SuperTileSummary default_composed = hbrick::composeSuperTile(
        parent_slot,
        children,
        brick_index.ports(),
        brick_index.seamEdges(),
        std::numeric_limits<uint64_t>::max()
    );
    EXPECT_EQ(default_composed.status, hbrick::BaselineStatus::Completed);
    EXPECT_FALSE(default_composed.interface_closure.test(0U, 0U) == false);
}

// Test 5: Partial parent group with fewer than g*g children (e.g. 3 children in L-shape)
TEST(C18PairSeed, PartialParentGroupWithThreeChildrenMatchesBaseline) {
    hbrick::TileSlot parent{.origin = {0U, 0U}, .extent = {8U, 8U}};
    hbrick::TileSlot slot_0{.origin = {0U, 0U}, .extent = {4U, 4U}};
    hbrick::TileSlot slot_1{.origin = {4U, 0U}, .extent = {4U, 4U}};
    hbrick::TileSlot slot_2{.origin = {0U, 4U}, .extent = {4U, 4U}};

    const std::vector<hbrick::GridCoord> p0 = {{3U, 1U}, {1U, 3U}};
    const std::vector<hbrick::GridCoord> p1 = {{4U, 1U}};
    const std::vector<hbrick::GridCoord> p2 = {{1U, 4U}};

    hbrick::BitMatrix s0(2U, 2U);
    s0.set(0U, 0U); s0.set(1U, 1U); s0.set(0U, 1U); s0.set(1U, 0U);
    hbrick::BitMatrix s1(1U, 1U);
    s1.set(0U, 0U);
    hbrick::BitMatrix s2(1U, 1U);
    s2.set(0U, 0U);

    const hbrick::ChildBoundarySummary children[] = {
        {.slot = slot_0, .port_coords = p0, .boundary_summary = &s0, .tile_index = 0U},
        {.slot = slot_1, .port_coords = p1, .boundary_summary = &s1, .tile_index = 1U},
        {.slot = slot_2, .port_coords = p2, .boundary_summary = &s2, .tile_index = 2U}
    };

    const hbrick::GammaOrdering gamma = hbrick::buildGammaOrdering(parent, children);

    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::BrickIndex brick_index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max(),
        true
    );

    auto get_pid = [&](uint32_t x, uint32_t y) {
        for (uint32_t p = 0U; p < brick_index.ports().numPorts(); ++p) {
            const auto& rec = brick_index.ports().port(p);
            if (rec.coord.x == x && rec.coord.y == y) return p;
        }
        return std::numeric_limits<uint32_t>::max();
    };

    std::vector<hbrick::SeamEdge> seams = {
        hbrick::SeamEdge{.from_port_id = get_pid(3U, 1U), .to_port_id = get_pid(4U, 1U)},
        hbrick::SeamEdge{.from_port_id = get_pid(1U, 3U), .to_port_id = get_pid(1U, 4U)}
    };

    const hbrick::BitMatrix iface_adj =
        hbrick::buildIfaceAdjacency(gamma, brick_index.ports(), seams);
    const hbrick::BitMatrix composed =
        hbrick::composeInterfaceAdjacency(gamma, children, iface_adj);

    hbrick::BitMatrix baseline_closure = hbrick::computeInterfaceClosure(composed);
    hbrick::PairSeedDiagnostics diag{};
    hbrick::BitMatrix pairseed_closure =
        hbrick::computeInterfaceClosurePairSeedExperimental(
            parent,
            children,
            brick_index.ports(),
            seams,
            gamma,
            composed,
            nullptr,
            &diag
        );

    EXPECT_TRUE(hbrick::bitMatricesEqual(baseline_closure, pairseed_closure));
    // Child 0 is adjacent to Child 1 (horizontal) and Child 2 (vertical).
    // Child 1 and Child 2 are diagonal, so not adjacent.
    // Total adjacent pairs = 2.
    EXPECT_EQ(diag.pair_count, 2U);
}

// Test 6: Randomized small parent-interface property test over 50 configurations
TEST(C18PairSeed, RandomizedParentInterfacePropertyTestMatchesBaseline) {
    std::mt19937_64 rng(0x12345678ULL);

    for (int trial = 0; trial < 50; ++trial) {
        // Build 2x2 grid of 4x4 tiles
        hbrick::TileSlot parent{.origin = {0U, 0U}, .extent = {8U, 8U}};
        hbrick::TileSlot slots[4] = {
            {.origin = {0U, 0U}, .extent = {4U, 4U}},
            {.origin = {4U, 0U}, .extent = {4U, 4U}},
            {.origin = {0U, 4U}, .extent = {4U, 4U}},
            {.origin = {4U, 4U}, .extent = {4U, 4U}}
        };

        // Decide which children are active (at least 2)
        std::vector<hbrick::ChildBoundarySummary> active;
        std::vector<std::vector<hbrick::GridCoord>> port_lists(4);
        std::vector<hbrick::BitMatrix> summaries(4);

        for (uint32_t c = 0; c < 4; ++c) {
            // Give each child 1-4 random ports on its boundary
            uint32_t num_p = 1U + static_cast<uint32_t>(rng() % 3U);
            for (uint32_t p = 0; p < num_p; ++p) {
                uint32_t x = slots[c].origin.x + static_cast<uint32_t>(rng() % 4U);
                uint32_t y = slots[c].origin.y + static_cast<uint32_t>(rng() % 4U);
                port_lists[c].push_back({x, y});
            }
            summaries[c] = hbrick::BitMatrix(num_p, num_p);
            for (uint32_t r = 0; r < num_p; ++r) {
                summaries[c].set(r, r);
                for (uint32_t col = 0; col < num_p; ++col) {
                    if ((rng() % 3U) == 0U) {
                        summaries[c].set(r, col);
                    }
                }
            }

            // Transitive closure on child summary
            hbrick::BooleanClosure::transitiveClosureWarshallInPlace(summaries[c]);

            active.push_back({
                .slot = slots[c],
                .port_coords = port_lists[c],
                .boundary_summary = &summaries[c],
                .tile_index = c
            });
        }

        const hbrick::GammaOrdering gamma = hbrick::buildGammaOrdering(parent, active);
        if (gamma.ports.empty()) continue;

        // Build mock port index and random seams
        hbrick::MazeLayout layout(8U, 8U, true);
        const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
            layout,
            hbrick::GridEdgeConversionMode::BidirectionalAll
        );
        const hbrick::BrickIndex brick_index = hbrick::BrickIndex::build(
            graph,
            layout,
            hbrick::TileSize{4U, 4U},
            std::numeric_limits<uint64_t>::max(),
            true
        );

        std::vector<hbrick::SeamEdge> seams;
        for (uint32_t i = 0; i < brick_index.ports().numPorts(); ++i) {
            for (uint32_t j = 0; j < brick_index.ports().numPorts(); ++j) {
                if (i != j && (rng() % 30U) == 0U) {
                    seams.push_back({.from_port_id = i, .to_port_id = j});
                }
            }
        }

        const hbrick::BitMatrix iface_adj =
            hbrick::buildIfaceAdjacency(gamma, brick_index.ports(), seams);
        const hbrick::BitMatrix composed =
            hbrick::composeInterfaceAdjacency(gamma, active, iface_adj);

        hbrick::BitMatrix baseline_closure = hbrick::computeInterfaceClosure(composed);
        hbrick::PairSeedDiagnostics diag{};
        hbrick::BitMatrix pairseed_closure =
            hbrick::computeInterfaceClosurePairSeedExperimental(
                parent,
                active,
                brick_index.ports(),
                seams,
                gamma,
                composed,
                nullptr,
                &diag
            );

        const bool equal = hbrick::bitMatricesEqual(baseline_closure, pairseed_closure);
        ASSERT_TRUE(equal) << "Randomized trial " << trial << " failed";
    }
}
