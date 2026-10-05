#include <gtest/gtest.h>

#include <limits>

#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/base_tile_summary.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/tile_boundary_order.hpp"
#include "hbrick/tile/tile_decomposition.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

hbrick::DirectedGridGraph buildGraph(hbrick::MazeLayout& layout) {
    return hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{42U, 0.5, 0.5, 0.0, 0.0}
    );
}

void expectPositiveAttachmentsMatchGlobalBfs(
    const hbrick::CsrGraph& graph,
    const hbrick::BaseTileSummary& summary,
    hbrick::GraphSearchScratch& scratch
) {
    for (uint32_t local_from = 0U; local_from < summary.numLocalVertices(); ++local_from) {
        const uint32_t global_from = summary.global_vertices[local_from];
        for (uint32_t port_to = 0U; port_to < summary.numPorts(); ++port_to) {
            if (!summary.vertex_to_boundary.test(local_from, port_to)) {
                continue;
            }
            const uint32_t global_to =
                summary.global_vertices[summary.ports[port_to].local_index];
            EXPECT_EQ(
                hbrick::Bfs::reachable(graph, global_from, global_to, scratch),
                hbrick::ReachabilityAnswer::Reachable
            );
        }
    }

    for (uint32_t port_from = 0U; port_from < summary.numPorts(); ++port_from) {
        const uint32_t global_from =
            summary.global_vertices[summary.ports[port_from].local_index];
        for (uint32_t local_to = 0U; local_to < summary.numLocalVertices(); ++local_to) {
            if (!summary.boundary_to_vertex.test(port_from, local_to)) {
                continue;
            }
            const uint32_t global_to = summary.global_vertices[local_to];
            EXPECT_EQ(
                hbrick::Bfs::reachable(graph, global_from, global_to, scratch),
                hbrick::ReachabilityAnswer::Reachable
            );
        }
    }
}

// Attachment matrices must be exact projections of local_closure onto
// (vertex, port) / (port, vertex) pairs — the same contract as
// expectBoundarySummaryMatchesLocalClosure for the port×port matrix.
// Positive attachment bits are also checked against global BFS by
// expectPositiveAttachmentsMatchGlobalBfs.  False-negatives from
// cross-tile re-entry are covered by AttachmentStaysZeroWhenOnlyReachableViaReentry
// and by exhaustive BRICK/H-BRICK oracle tests.
void expectAttachmentIsConservativePositiveOracle(
    const hbrick::BaseTileSummary& summary
) {
    for (uint32_t local_from = 0U; local_from < summary.numLocalVertices(); ++local_from) {
        for (uint32_t port_to = 0U; port_to < summary.numPorts(); ++port_to) {
            const uint32_t port_local = summary.ports[port_to].local_index;
            EXPECT_EQ(
                summary.vertex_to_boundary.test(local_from, port_to),
                summary.local_closure.test(local_from, port_local)
            ) << "vertex_to_boundary must match local_closure projection";
        }
    }
    for (uint32_t port_from = 0U; port_from < summary.numPorts(); ++port_from) {
        const uint32_t port_local = summary.ports[port_from].local_index;
        for (uint32_t local_to = 0U; local_to < summary.numLocalVertices(); ++local_to) {
            EXPECT_EQ(
                summary.boundary_to_vertex.test(port_from, local_to),
                summary.local_closure.test(port_local, local_to)
            ) << "boundary_to_vertex must match local_closure projection";
        }
    }
}

[[nodiscard]] uint32_t localIndexOfCoord(
    const hbrick::BaseTileSummary& summary,
    const hbrick::GridCoord coord
) {
    for (uint32_t local = 0U; local < summary.numLocalVertices(); ++local) {
        if (summary.local_coords[local].x == coord.x
            && summary.local_coords[local].y == coord.y) {
            return local;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

[[nodiscard]] uint32_t portIndexOfCoord(
    const hbrick::BaseTileSummary& summary,
    const hbrick::GridCoord coord
) {
    for (uint32_t port = 0U; port < summary.numPorts(); ++port) {
        if (summary.ports[port].coord.x == coord.x
            && summary.ports[port].coord.y == coord.y) {
            return port;
        }
    }
    return std::numeric_limits<uint32_t>::max();
}

void expectBoundarySummaryMatchesLocalClosure(const hbrick::BaseTileSummary& summary) {
    for (uint32_t port_from = 0U; port_from < summary.numPorts(); ++port_from) {
        const uint32_t local_from = summary.ports[port_from].local_index;
        for (uint32_t port_to = 0U; port_to < summary.numPorts(); ++port_to) {
            const uint32_t local_to = summary.ports[port_to].local_index;
            EXPECT_EQ(
                summary.boundary_summary.test(port_from, port_to),
                summary.local_closure.test(local_from, local_to)
            );
        }
    }
}

void expectPositiveBoundaryPairsReachableGlobally(
    const hbrick::CsrGraph& graph,
    const hbrick::BaseTileSummary& summary,
    hbrick::GraphSearchScratch& scratch
) {
    for (uint32_t port_from = 0U; port_from < summary.numPorts(); ++port_from) {
        const uint32_t global_from =
            summary.global_vertices[summary.ports[port_from].local_index];
        for (uint32_t port_to = 0U; port_to < summary.numPorts(); ++port_to) {
            if (!summary.boundary_summary.test(port_from, port_to)) {
                continue;
            }
            const uint32_t global_to =
                summary.global_vertices[summary.ports[port_to].local_index];
            EXPECT_EQ(
                hbrick::Bfs::reachable(graph, global_from, global_to, scratch),
                hbrick::ReachabilityAnswer::Reachable
            );
        }
    }
}

}  // namespace

TEST(BaseTileSummary, CanonicalBoundaryOrderMatchesPortSidePrecedence) {
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(4U, 4U, hbrick::TileSize{4U, 4U});
    const hbrick::TileSlot& slot = decomposition.slot(0U, 0U);
    const std::vector<hbrick::GridCoord> boundary = hbrick::canonicalBoundaryCoords(slot);

    ASSERT_EQ(boundary.size(), 12U);
    EXPECT_EQ(boundary.front(), (hbrick::GridCoord{0U, 0U}));
    EXPECT_EQ(boundary[3U], (hbrick::GridCoord{3U, 0U}));
    EXPECT_EQ(boundary[4U], (hbrick::GridCoord{3U, 1U}));
    EXPECT_EQ(boundary.back(), (hbrick::GridCoord{0U, 2U}));
}

TEST(BaseTileSummary, SingleTileProjectionsMatchClosure) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{1U, 1U}, false);
    const hbrick::DirectedGridGraph graph = buildGraph(layout);
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{4U, 4U});

    const hbrick::BaseTileSummary summary = hbrick::buildBaseTile(
        graph,
        layout,
        decomposition.slot(0U, 0U),
        std::numeric_limits<uint64_t>::max()
    );

    ASSERT_EQ(summary.status, hbrick::BaselineStatus::Completed);
    EXPECT_EQ(summary.numLocalVertices(), 15U);
    EXPECT_GT(summary.numPorts(), 0U);
    expectBoundarySummaryMatchesLocalClosure(summary);

    hbrick::GraphSearchScratch scratch(graph.numVertices());
    expectPositiveBoundaryPairsReachableGlobally(graph.csrGraph(), summary, scratch);
    expectPositiveAttachmentsMatchGlobalBfs(graph.csrGraph(), summary, scratch);
    expectAttachmentIsConservativePositiveOracle(summary);
}

TEST(BaseTileSummary, BrickTileIndexMapsGlobalVertices) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{2U, 2U}, false);
    const hbrick::DirectedGridGraph graph = buildGraph(layout);
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{4U, 4U});

    const hbrick::BrickTileIndex index = hbrick::BrickTileIndex::build(
        graph,
        layout,
        decomposition,
        std::numeric_limits<uint64_t>::max()
    );

    ASSERT_TRUE(index.allTilesCompleted());
    EXPECT_EQ(index.summaries().size(), 4U);

    const hbrick::GridCoord coord{5U, 1U};
    const uint32_t global_vertex = layout.vertexId(coord).value;
    const uint32_t tile_index = index.tileIndexForGlobalVertex(global_vertex);
    ASSERT_NE(tile_index, std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(tile_index, 1U);

    const uint32_t local_index = index.localIndexForGlobalVertex(global_vertex);
    ASSERT_NE(local_index, std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(
        index.summaryByIndex(tile_index).global_vertices[local_index],
        global_vertex
    );

    const uint32_t blocked_vertex = layout.vertexId(hbrick::GridCoord{2U, 2U}).value;
    EXPECT_EQ(
        index.localIndexForGlobalVertex(blocked_vertex),
        std::numeric_limits<uint32_t>::max()
    );
}

TEST(BaseTileSummary, SkippedByPolicyWhenMemoryBudgetExceeded) {
    hbrick::MazeLayout layout(4U, 4U);
    const hbrick::DirectedGridGraph graph = buildGraph(layout);
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(4U, 4U, hbrick::TileSize{4U, 4U});

    const hbrick::BaseTileSummary summary = hbrick::buildBaseTile(
        graph,
        layout,
        decomposition.slot(0U, 0U),
        0U
    );

    EXPECT_EQ(summary.status, hbrick::BaselineStatus::SkippedByPolicy);
    EXPECT_TRUE(summary.local_closure.numRows() == 0U);
}

TEST(BaseTileSummary, EightByEightTileSummariesAgainstGlobalBfs) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{3U, 3U}, false);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    const hbrick::DirectedGridGraph graph = buildGraph(layout);
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 8U, hbrick::TileSize{4U, 4U});

    const hbrick::BrickTileIndex index = hbrick::BrickTileIndex::build(
        graph,
        layout,
        decomposition,
        std::numeric_limits<uint64_t>::max()
    );

    ASSERT_TRUE(index.allTilesCompleted());
    hbrick::GraphSearchScratch scratch(graph.numVertices());

    for (const hbrick::BaseTileSummary& summary : index.summaries()) {
        expectBoundarySummaryMatchesLocalClosure(summary);
        expectPositiveBoundaryPairsReachableGlobally(graph.csrGraph(), summary, scratch);
        expectPositiveAttachmentsMatchGlobalBfs(graph.csrGraph(), summary, scratch);
        expectAttachmentIsConservativePositiveOracle(summary);
    }
}

TEST(BaseTileSummary, AttachmentStaysZeroWhenOnlyReachableViaReentry) {
    // 8×4 map, two 4×4 tiles.  A horizontal wall splits the left tile so
    // bottom-left B cannot reach top-right port P intra-tile, but can via
    // the right tile (leave at bottom, re-enter at top).
    //
    //   Left tile          Right tile
    //   . . . P | . . . .
    //   # # # # | . . . .
    //   # # # # | . . . .
    //   B . . . | . . . .
    hbrick::MazeLayout layout(8U, 4U, true);
    for (uint32_t x = 0U; x < 4U; ++x) {
        layout.setPassable(hbrick::GridCoord{x, 1U}, false);
        layout.setPassable(hbrick::GridCoord{x, 2U}, false);
    }

    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(8U, 4U, hbrick::TileSize{4U, 4U});

    const hbrick::BaseTileSummary summary = hbrick::buildBaseTile(
        graph,
        layout,
        decomposition.slot(0U, 0U),
        std::numeric_limits<uint64_t>::max()
    );
    ASSERT_EQ(summary.status, hbrick::BaselineStatus::Completed);

    const hbrick::GridCoord source_coord{0U, 3U};
    const hbrick::GridCoord port_coord{3U, 0U};
    const uint32_t local_from = localIndexOfCoord(summary, source_coord);
    const uint32_t port_to = portIndexOfCoord(summary, port_coord);
    ASSERT_NE(local_from, std::numeric_limits<uint32_t>::max());
    ASSERT_NE(port_to, std::numeric_limits<uint32_t>::max());

    EXPECT_FALSE(summary.vertex_to_boundary.test(local_from, port_to))
        << "attachment must stay 0 when the only path leaves the tile";
    EXPECT_FALSE(
        summary.local_closure.test(local_from, summary.ports[port_to].local_index)
    );

    const uint32_t global_from = layout.vertexId(source_coord).value;
    const uint32_t global_to = layout.vertexId(port_coord).value;
    hbrick::GraphSearchScratch scratch(graph.numVertices());
    EXPECT_EQ(
        hbrick::Bfs::reachable(graph.csrGraph(), global_from, global_to, scratch),
        hbrick::ReachabilityAnswer::Reachable
    ) << "global re-entry path B → right tile → P must exist";

    expectAttachmentIsConservativePositiveOracle(summary);
}

TEST(BaseTileSummary, ImpassableBaseTileProducesEmptyCompletedSummary) {
    // 4x4 map completely blocked by walls
    hbrick::MazeLayout layout(4U, 4U, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(4U, 4U, hbrick::TileSize{4U, 4U});

    const hbrick::BaseTileSummary summary = hbrick::buildBaseTile(
        graph,
        layout,
        decomposition.slot(0U, 0U),
        std::numeric_limits<uint64_t>::max()
    );

    EXPECT_EQ(summary.status, hbrick::BaselineStatus::Completed);
    EXPECT_EQ(summary.numLocalVertices(), 0U);
    EXPECT_EQ(summary.numPorts(), 0U);
    EXPECT_EQ(summary.local_closure.numRows(), 0U);
    EXPECT_TRUE(summary.global_vertices.empty());
    EXPECT_TRUE(summary.ports.empty());
}

TEST(BaseTileSummary, InsufficientTileBudgetProducesOutOfMemoryStatus) {
    // 4x4 passable tile, but given only 1 byte of budget
    hbrick::MazeLayout layout(4U, 4U, true);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const hbrick::TileDecomposition decomposition =
        hbrick::TileDecomposition::decompose(4U, 4U, hbrick::TileSize{4U, 4U});

    // 1 byte budget is strictly insufficient for 16 local vertices closure + attachments
    const hbrick::BaseTileSummary summary = hbrick::buildBaseTile(
        graph,
        layout,
        decomposition.slot(0U, 0U),
        1U
    );

    EXPECT_TRUE(summary.status == hbrick::BaselineStatus::OutOfMemory
                || summary.status == hbrick::BaselineStatus::SkippedByPolicy);
}
