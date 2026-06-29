#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/brick_index.hpp"
#include "hbrick/tile/brick_tile_index.hpp"
#include "hbrick/tile/port_graph.hpp"
#include "hbrick/tile/port_index.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

std::string edgeKey(uint32_t from, uint32_t to) {
    return std::to_string(from) + "->" + std::to_string(to);
}

hbrick::DirectedGridGraph buildGraph(hbrick::MazeLayout& layout) {
    return hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{7U, 0.6, 0.4, 0.0, 0.0}
    );
}

uint32_t countUniqueManualSeamEdges(
    const hbrick::CsrGraph& graph,
    const hbrick::BrickTileIndex& tile_index,
    const hbrick::PortIndex& port_index
) {
    std::set<std::pair<uint32_t, uint32_t>> unique;
    for (uint32_t global_from = 0U; global_from < graph.numVertices(); ++global_from) {
        const uint32_t from_port_id = port_index.portIdForGlobalVertex(global_from);
        if (from_port_id == std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        const uint32_t from_tile = tile_index.tileIndexForGlobalVertex(global_from);
        for (const uint32_t global_to : graph.outNeighbors(global_from)) {
            const uint32_t to_port_id = port_index.portIdForGlobalVertex(global_to);
            if (to_port_id == std::numeric_limits<uint32_t>::max()) {
                continue;
            }
            const uint32_t to_tile = tile_index.tileIndexForGlobalVertex(global_to);
            if (from_tile != to_tile) {
                unique.insert({from_port_id, to_port_id});
            }
        }
    }
    return static_cast<uint32_t>(unique.size());
}

void expectPortGraphMatchesGlobalBfsOnPortPairs(
    const hbrick::CsrGraph& global_graph,
    const hbrick::CsrGraph& port_graph,
    const hbrick::PortIndex& port_index
) {
    hbrick::GraphSearchScratch global_scratch(global_graph.numVertices());
    hbrick::GraphSearchScratch port_scratch(port_graph.numVertices());

    for (uint32_t from_port = 0U; from_port < port_index.numPorts(); ++from_port) {
        for (uint32_t to_port = 0U; to_port < port_index.numPorts(); ++to_port) {
            const uint32_t global_from = port_index.port(from_port).global_vertex;
            const uint32_t global_to = port_index.port(to_port).global_vertex;

            const hbrick::ReachabilityAnswer global_answer = hbrick::Bfs::reachable(
                global_graph,
                global_from,
                global_to,
                global_scratch
            );
            const hbrick::ReachabilityAnswer port_answer = hbrick::Bfs::reachable(
                port_graph,
                from_port,
                to_port,
                port_scratch
            );

            if (global_answer == hbrick::ReachabilityAnswer::Reachable) {
                EXPECT_EQ(port_answer, hbrick::ReachabilityAnswer::Reachable)
                    << "global reachable port pair (" << from_port << "," << to_port << ")";
            } else {
                EXPECT_EQ(port_answer, hbrick::ReachabilityAnswer::Unreachable)
                    << "global unreachable port pair (" << from_port << "," << to_port << ")";
            }
        }
    }
}

}  // namespace

TEST(PortGraph, SeamEdgeDeduperRejectsDuplicatePortPairs) {
    hbrick::SeamEdgeDeduper deduper;
    EXPECT_TRUE(deduper.tryInsert(3U, 7U));
    EXPECT_FALSE(deduper.tryInsert(3U, 7U));
    EXPECT_TRUE(deduper.tryInsert(3U, 8U));
    EXPECT_TRUE(deduper.tryInsert(4U, 7U));

    deduper.clear();
    EXPECT_TRUE(deduper.tryInsert(1U, 2U));
    EXPECT_FALSE(deduper.tryInsert(1U, 2U));
}

TEST(PortGraph, SeamEdgeCountMatchesManualScan) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{3U, 3U}, false);
    const hbrick::DirectedGridGraph graph = buildGraph(layout);

    const hbrick::BrickIndex index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max(),
        true
    );

    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    EXPECT_EQ(
        index.seamEdges().size(),
        countUniqueManualSeamEdges(graph.csrGraph(), index.tiles(), index.ports())
    );
}

TEST(PortGraph, IncludesCompressedIntraTileEdges) {
    hbrick::MazeLayout layout(4U, 4U);
    const hbrick::DirectedGridGraph graph = buildGraph(layout);

    const hbrick::BrickIndex index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );

    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    const hbrick::BaseTileSummary& summary = index.tiles().summary(0U, 0U);
    ASSERT_GE(summary.numPorts(), 2U);

    uint32_t compressed_edges = 0U;
    for (uint32_t port_from = 0U; port_from < summary.numPorts(); ++port_from) {
        for (uint32_t port_to = 0U; port_to < summary.numPorts(); ++port_to) {
            if (summary.boundary_summary.test(port_from, port_to) && port_from != port_to) {
                ++compressed_edges;
            }
        }
    }
    EXPECT_GE(index.portGraph().numEdges(), compressed_edges);
}

TEST(PortGraph, BidirectionalAllSixteenBySixteenOracle) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{5U, 5U}, false);
    layout.setPassable(hbrick::GridCoord{10U, 10U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const hbrick::BrickIndex index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );

    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    EXPECT_GT(index.ports().numPorts(), 0U);
    expectPortGraphMatchesGlobalBfsOnPortPairs(
        graph.csrGraph(),
        index.portGraph(),
        index.ports()
    );
}

TEST(PortGraph, RandomAsymmetricEightByEightOracle) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{2U, 2U}, false);
    layout.setPassable(hbrick::GridCoord{5U, 5U}, false);
    const hbrick::DirectedGridGraph graph = buildGraph(layout);

    const hbrick::BrickIndex index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );

    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    expectPortGraphMatchesGlobalBfsOnPortPairs(
        graph.csrGraph(),
        index.portGraph(),
        index.ports()
    );
}

TEST(PortGraph, SeamEdgesAreCrossTileOnly) {
    hbrick::MazeLayout layout(8U, 8U);
    const hbrick::DirectedGridGraph graph = buildGraph(layout);
    const hbrick::BrickIndex index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max(),
        true
    );

    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    for (const hbrick::SeamEdge& seam_edge : index.seamEdges()) {
        const hbrick::PortRecord& from_port = index.ports().port(seam_edge.from_port_id);
        const hbrick::PortRecord& to_port = index.ports().port(seam_edge.to_port_id);
        EXPECT_NE(from_port.tile_index, to_port.tile_index);
    }
}

TEST(PortGraph, PortIndexMapsGlobalVerticesUniquely) {
    hbrick::MazeLayout layout(8U, 8U);
    const hbrick::DirectedGridGraph graph = buildGraph(layout);
    const hbrick::BrickIndex index = hbrick::BrickIndex::build(
        graph,
        layout,
        hbrick::TileSize{4U, 4U},
        std::numeric_limits<uint64_t>::max()
    );

    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    std::set<std::string> seen;
    for (uint32_t port_id = 0U; port_id < index.ports().numPorts(); ++port_id) {
        const hbrick::PortRecord& record = index.ports().port(port_id);
        EXPECT_EQ(index.ports().portIdForGlobalVertex(record.global_vertex), port_id);
        const auto insert_result = seen.insert(edgeKey(record.global_vertex, port_id));
        EXPECT_TRUE(insert_result.second);
    }
}
