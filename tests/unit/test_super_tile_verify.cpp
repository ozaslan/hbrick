#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "hbrick/bit/boolean_closure.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/group_size.hpp"
#include "hbrick/tile/hierarchy_tree.hpp"
#include "hbrick/tile/super_tile_composer.hpp"
#include "hbrick/tile/super_tile_oracle.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

hbrick::TileSlot parentSlotFromRegion(const hbrick::RegionNode& region) {
    hbrick::TileSlot slot{};
    slot.tile_i = region.region_i;
    slot.tile_j = region.region_j;
    slot.origin = region.origin;
    slot.extent = region.extent;
    return slot;
}

hbrick::SuperTileSummary composeParentRegion(
    const hbrick::BrickIndex& brick_index,
    const hbrick::RegionNode& parent_region
) {
    const hbrick::TileSlot parent_slot = parentSlotFromRegion(parent_region);

    std::vector<hbrick::ChildBoundarySummary> children;
    std::vector<std::vector<hbrick::GridCoord>> child_port_storage;
    child_port_storage.reserve(parent_region.children.size());
    children.reserve(parent_region.children.size());

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

    return hbrick::composeSuperTile(
        parent_slot,
        children,
        brick_index.ports(),
        brick_index.seamEdges(),
        std::numeric_limits<uint64_t>::max()
    );
}

void expectOracleAGate(
    const hbrick::BrickIndex& brick_index,
    const hbrick::SuperTileSummary& composed,
    const hbrick::TileSlot& parent_slot
) {
    ASSERT_EQ(composed.status, hbrick::BaselineStatus::Completed);

    const hbrick::GammaOrdering gamma =
        hbrick::buildRegionGammaFromPortIndex(brick_index, parent_slot);
    ASSERT_EQ(gamma.ports.size(), composed.gamma.ports.size());

    const hbrick::BitMatrix flat_boundary = hbrick::buildFlatRegionPortBoundarySummary(
        brick_index,
        parent_slot,
        composed.gamma,
        std::numeric_limits<uint64_t>::max()
    );

    EXPECT_TRUE(hbrick::bitMatricesEqual(composed.boundary_summary, flat_boundary));
}

void expectOracleBGate(
    const hbrick::DirectedGridGraph& graph,
    const hbrick::MazeLayout& layout,
    const hbrick::SuperTileSummary& composed,
    const hbrick::TileSlot& parent_slot
) {
    ASSERT_EQ(composed.status, hbrick::BaselineStatus::Completed);

    const hbrick::RegionCellClosure region_closure = hbrick::buildRegionCellClosure(
        graph,
        layout,
        parent_slot,
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(region_closure.status, hbrick::BaselineStatus::Completed);

    EXPECT_TRUE(hbrick::boundarySummaryMatchesCellClosure(
        region_closure,
        composed.exterior_ports,
        composed.boundary_summary
    ));
}

}  // namespace

TEST(SuperTileVerify, OracleA_EightByEightGroupedParentMatchesFlatPortGraph) {
    hbrick::MazeLayout layout(8U, 8U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const hbrick::BrickIndex brick_index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(brick_index.status(), hbrick::BaselineStatus::Completed);

    const hbrick::HierarchyTree tree = hbrick::HierarchyTree::build(
        brick_index.tiles().decomposition(),
        hbrick::GroupSize{2U, 2U},
        2U
    );
    const hbrick::RegionNode& parent_region = tree.node(1U, 0U);
    const hbrick::SuperTileSummary composed = composeParentRegion(brick_index, parent_region);

    expectOracleAGate(brick_index, composed, parentSlotFromRegion(parent_region));
}

TEST(SuperTileVerify, OracleA_RandomAsymmetricMapWithObstaclesMatchesFlatPortGraph) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{3U, 3U}, false);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{19U, 0.55, 0.45, 0.0, 0.0}
    );

    const hbrick::BrickIndex brick_index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(brick_index.status(), hbrick::BaselineStatus::Completed);

    const hbrick::HierarchyTree tree = hbrick::HierarchyTree::build(
        brick_index.tiles().decomposition(),
        hbrick::GroupSize{2U, 2U},
        2U
    );
    const hbrick::RegionNode& parent_region = tree.node(1U, 0U);
    const hbrick::SuperTileSummary composed = composeParentRegion(brick_index, parent_region);

    expectOracleAGate(brick_index, composed, parentSlotFromRegion(parent_region));
}

TEST(SuperTileVerify, OracleB_TinyMapGroupedParentMatchesCellRegionClosure) {
    hbrick::MazeLayout layout(6U, 6U);
    layout.setPassable(hbrick::GridCoord{2U, 2U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const hbrick::BrickIndex brick_index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{3U, 3U},
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(brick_index.status(), hbrick::BaselineStatus::Completed);

    const hbrick::HierarchyTree tree = hbrick::HierarchyTree::build(
        brick_index.tiles().decomposition(),
        hbrick::GroupSize{2U, 2U},
        2U
    );
    const hbrick::RegionNode& parent_region = tree.node(1U, 0U);
    const hbrick::SuperTileSummary composed = composeParentRegion(brick_index, parent_region);

    expectOracleBGate(graph, layout, composed, parentSlotFromRegion(parent_region));
}

TEST(SuperTileVerify, OracleB_PartialEdgeMapMatchesCellRegionClosure) {
    hbrick::MazeLayout layout(9U, 7U);
    layout.setPassable(hbrick::GridCoord{5U, 2U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const hbrick::BrickIndex brick_index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(brick_index.status(), hbrick::BaselineStatus::Completed);

    const hbrick::HierarchyTree tree = hbrick::HierarchyTree::build(
        brick_index.tiles().decomposition(),
        hbrick::GroupSize{2U, 2U},
        8U
    );
    ASSERT_GE(tree.numLevels(), 2U);
    const hbrick::RegionNode& parent_region = tree.node(tree.numLevels() - 1U, 0U);
    const hbrick::SuperTileSummary composed = composeParentRegion(brick_index, parent_region);

    expectOracleBGate(graph, layout, composed, parentSlotFromRegion(parent_region));
    expectOracleAGate(brick_index, composed, parentSlotFromRegion(parent_region));
}
