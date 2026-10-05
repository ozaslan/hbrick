#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <limits>
#include <vector>

#include "hbrick/baselines/hbrick_baseline.hpp"
#include "hbrick/baselines/hbrick_fused_lift_baseline.hpp"
#include "hbrick/baselines/hbrick_fused_lift_cache_baseline.hpp"
#include "hbrick/baselines/hbrick_hierarchy_query.hpp"
#include "hbrick/baselines/hbrick_skip_lift_baseline.hpp"
#include "hbrick/baselines/hbrick_scc_label_baseline.hpp"
#include "hbrick/bit/bit_vector.hpp"
#include "hbrick/bit/bit_matrix.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/graph/bfs.hpp"
#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/graph/graph_search_scratch.hpp"
#include "hbrick/graph/random_asymmetric_params.hpp"
#include "hbrick/grid/maze_layout.hpp"
#include "hbrick/tile/hbrick_config.hpp"
#include "hbrick/tile/hbrick_index.hpp"
#include "hbrick/tile/fused_tree_lifts.hpp"
#include "hbrick/tile/skip_level_lifts.hpp"
#include "hbrick/tile/tile_size.hpp"

namespace {

hbrick::HBrickConfig configFor(
    const hbrick::TileSize base_tile_size,
    const hbrick::GroupSize group_size,
    const uint32_t max_depth
) {
    hbrick::HBrickConfig config{};
    config.base_tile_size = base_tile_size;
    config.group_size = group_size;
    config.max_depth = max_depth;
    config.max_memory_bytes = std::numeric_limits<uint64_t>::max();
    return config;
}

[[nodiscard]] hbrick::BitMatrix rowProductForTest(
    const hbrick::BitMatrix& left,
    const hbrick::BitMatrix& right
) {
    hbrick::BitMatrix out(left.numRows(), right.numCols());
    const uint32_t join = std::min(left.numCols(), right.numRows());
    for (uint32_t row_index = 0U; row_index < left.numRows(); ++row_index) {
        const hbrick::BitVector& row = left.row(row_index);
        for (size_t word_index = 0U; word_index < row.numWords(); ++word_index) {
            uint64_t word = row.word(word_index);
            const size_t bit_base = word_index * 64U;
            if (bit_base >= join) {
                break;
            }
            while (word != 0U) {
                const unsigned bit = static_cast<unsigned>(std::countr_zero(word));
                word &= word - 1U;
                const uint32_t col = static_cast<uint32_t>(bit_base + bit);
                if (col < join) {
                    out.row(row_index).rowOrFrom(right.row(col));
                }
            }
        }
    }
    return out;
}

[[nodiscard]] bool sameMatrix(
    const hbrick::BitMatrix& left,
    const hbrick::BitMatrix& right
) {
    if (left.numRows() != right.numRows() || left.numCols() != right.numCols()) {
        return false;
    }
    for (uint32_t row = 0U; row < left.numRows(); ++row) {
        if (left.row(row).numWords() != right.row(row).numWords()) {
            return false;
        }
        for (size_t word = 0U; word < left.row(row).numWords(); ++word) {
            if (left.row(row).word(word) != right.row(row).word(word)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] uint32_t embeddingIndexForTest(
    const hbrick::RegionNode& child,
    const hbrick::SuperTileSummary& parent
) {
    const uint32_t slot = child.child_slot_in_parent;
    if (slot >= parent.child_embedding_of.size()) {
        return UINT32_MAX;
    }
    const uint32_t embedding = parent.child_embedding_of[slot];
    return embedding < parent.child_port_to_gamma.size() ? embedding : UINT32_MAX;
}

[[nodiscard]] bool adaptiveM4rmEligibleForTest(
    const hbrick::BitMatrix& left,
    const hbrick::BitMatrix& right
) {
    const uint32_t m = left.numRows();
    const uint32_t k = std::min(left.numCols(), right.numRows());
    const uint32_t n = right.numCols();
    if (n == 0U || m < 24U || k < 64U || n < 16U) {
        return false;
    }
    const double density = static_cast<double>(left.popcount())
        / static_cast<double>(static_cast<uint64_t>(m) * k);
    if (density <= 0.125) {
        return false;
    }
    return (density >= 0.30 && m >= 32U)
        || ((8.0 * density - 1.0) * static_cast<double>(m) > 256.0);
}

}  // namespace

TEST(HBrickFusedLiftBaseline, MatchesBfsOnEightByEightHierarchy) {
    hbrick::MazeLayout layout(8U, 8U);
    layout.setPassable(hbrick::GridCoord{2U, 2U}, false);
    layout.setPassable(hbrick::GridCoord{5U, 5U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{42U, 0.6, 0.4, 0.0, 0.0}
    );

    const auto config = configFor(hbrick::TileSize{4U, 4U}, hbrick::GroupSize{2U, 2U}, 2U);

    hbrick::HBrickBaseline standard_hbrick;
    standard_hbrick.preprocess(graph, layout, config);
    ASSERT_EQ(standard_hbrick.status(), hbrick::BaselineStatus::Completed);

    hbrick::HBrickFusedLiftBaseline fused_lift;
    fused_lift.preprocess(graph, layout, config);
    ASSERT_EQ(fused_lift.status(), hbrick::BaselineStatus::Completed);

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    const hbrick::CsrGraph& csr = graph.csrGraph();

    for (uint32_t s = 0U; s < graph.numVertices(); ++s) {
        for (uint32_t t = 0U; t < graph.numVertices(); ++t) {
            const hbrick::ReachabilityAnswer expected =
                hbrick::Bfs::reachable(csr, s, t, bfs_scratch);
            const hbrick::ReachabilityAnswer standard_ans = standard_hbrick.query(s, t);
            const hbrick::ReachabilityAnswer fused_ans = fused_lift.query(s, t);

            ASSERT_EQ(standard_ans, expected) << "s=" << s << " t=" << t;
            ASSERT_EQ(fused_ans, expected) << "s=" << s << " t=" << t;
        }
    }
}

TEST(HBrickFusedLiftCacheBaseline, MatchesBfsAndWarmCacheHits) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{3U, 3U}, false);
    layout.setPassable(hbrick::GridCoord{7U, 7U}, false);
    layout.setPassable(hbrick::GridCoord{11U, 11U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{99U, 0.55, 0.45, 0.0, 0.0}
    );

    const auto config = configFor(
        hbrick::TileSize{4U, 4U},
        hbrick::GroupSize{2U, 2U},
        hbrick::kHBrickFullDepth
    );

    hbrick::HBrickFusedLiftCacheBaseline cached_hbrick(16U);
    cached_hbrick.preprocess(graph, layout, config);
    ASSERT_EQ(cached_hbrick.status(), hbrick::BaselineStatus::Completed);

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    const hbrick::CsrGraph& csr = graph.csrGraph();

    // Pass 1: Cold queries and warm repeat checks.
    for (uint32_t s = 0U; s < graph.numVertices(); s += 5U) {
        for (uint32_t t = 0U; t < graph.numVertices(); t += 5U) {
            const hbrick::ReachabilityAnswer expected =
                hbrick::Bfs::reachable(csr, s, t, bfs_scratch);

            // Cold query
            const hbrick::ReachabilityAnswer cold_ans = cached_hbrick.query(s, t);
            ASSERT_EQ(cold_ans, expected) << "Cold: s=" << s << " t=" << t;

            // Warm query (exercises cached source and target chains)
            const hbrick::ReachabilityAnswer warm_ans = cached_hbrick.query(s, t);
            ASSERT_EQ(warm_ans, expected) << "Warm: s=" << s << " t=" << t;
        }
    }

    // Clear cache and verify queries continue to work correctly.
    cached_hbrick.clearCache();
    for (uint32_t s = 0U; s < 30U; ++s) {
        for (uint32_t t = 0U; t < 30U; ++t) {
            const hbrick::ReachabilityAnswer expected =
                hbrick::Bfs::reachable(csr, s, t, bfs_scratch);
            ASSERT_EQ(cached_hbrick.query(s, t), expected)
                << "Post-clear: s=" << s << " t=" << t;
        }
    }
}

TEST(HBrickSccLabelBaseline, MatchesBfsOnSixteenBySixteenHierarchy) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    layout.setPassable(hbrick::GridCoord{8U, 8U}, false);
    layout.setPassable(hbrick::GridCoord{12U, 12U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{123U, 0.55, 0.45, 0.0, 0.0}
    );

    const auto config = configFor(
        hbrick::TileSize{4U, 4U},
        hbrick::GroupSize{2U, 2U},
        hbrick::kHBrickFullDepth
    );

    hbrick::HBrickSccLabelBaseline scc_baseline;
    scc_baseline.preprocess(graph, layout, config);
    ASSERT_EQ(scc_baseline.status(), hbrick::BaselineStatus::Completed);
    EXPECT_GT(scc_baseline.indexStorageBytes(), 0U);

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    const hbrick::CsrGraph& csr = graph.csrGraph();

    for (uint32_t s = 0U; s < graph.numVertices(); ++s) {
        for (uint32_t t = 0U; t < graph.numVertices(); ++t) {
            const hbrick::ReachabilityAnswer expected =
                hbrick::Bfs::reachable(csr, s, t, bfs_scratch);
            const hbrick::ReachabilityAnswer actual = scc_baseline.query(s, t);
            ASSERT_EQ(actual, expected) << "s=" << s << " t=" << t;
        }
    }
}

TEST(HBrickAllNewBaselines, AgreementAcrossAllMethods) {
    hbrick::MazeLayout layout(16U, 16U);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );

    const auto config = configFor(
        hbrick::TileSize{4U, 4U},
        hbrick::GroupSize{2U, 2U},
        2U
    );

    hbrick::HBrickBaseline standard;
    standard.preprocess(graph, layout, config);

    hbrick::HBrickFusedLiftBaseline fused;
    fused.preprocess(graph, layout, config);

    hbrick::HBrickFusedLiftCacheBaseline cached(8U);
    cached.preprocess(graph, layout, config);

    hbrick::HBrickSccLabelBaseline scc;
    scc.preprocess(graph, layout, config);

    hbrick::HBrickSkipLiftBaseline skip;
    skip.preprocess(graph, layout, config);

    for (uint32_t s = 0U; s < graph.numVertices(); s += 7U) {
        for (uint32_t t = 0U; t < graph.numVertices(); t += 7U) {
            const auto a1 = standard.query(s, t);
            const auto a2 = fused.query(s, t);
            const auto a3 = cached.query(s, t);
            const auto a4 = scc.query(s, t);
            const auto a5 = skip.query(s, t);

            EXPECT_EQ(a1, a2);
            EXPECT_EQ(a1, a3);
            EXPECT_EQ(a1, a4);
            EXPECT_EQ(a1, a5);
        }
    }
}

[[nodiscard]] bool sameBits(const hbrick::BitVector& left, const hbrick::BitVector& right) {
    const size_t num_words = std::max(left.numWords(), right.numWords());
    for (size_t w = 0U; w < num_words; ++w) {
        if (left.word(w) != right.word(w)) {
            return false;
        }
    }
    return true;
}

TEST(HBrickSkipLiftBaseline, MatchesBfsOnSixteenBySixteenFullDepth) {
    hbrick::MazeLayout layout(16U, 16U);
    layout.setPassable(hbrick::GridCoord{4U, 4U}, false);
    layout.setPassable(hbrick::GridCoord{8U, 8U}, false);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::RandomAsymmetric,
        hbrick::RandomAsymmetricParams{77U, 0.55, 0.45, 0.0, 0.0}
    );

    const auto config = configFor(
        hbrick::TileSize{4U, 4U},
        hbrick::GroupSize{2U, 2U},
        hbrick::kHBrickFullDepth
    );

    hbrick::HBrickFusedLiftBaseline fused;
    fused.preprocess(graph, layout, config);
    ASSERT_EQ(fused.status(), hbrick::BaselineStatus::Completed);

    hbrick::HBrickSkipLiftBaseline skip;
    skip.preprocess(graph, layout, config);
    ASSERT_EQ(skip.status(), hbrick::BaselineStatus::Completed);
    ASSERT_TRUE(skip.skipLifts().isValid());

    hbrick::GraphSearchScratch bfs_scratch(graph.numVertices());
    const hbrick::CsrGraph& csr = graph.csrGraph();

    for (uint32_t s = 0U; s < graph.numVertices(); ++s) {
        for (uint32_t t = 0U; t < graph.numVertices(); ++t) {
            const hbrick::ReachabilityAnswer expected =
                hbrick::Bfs::reachable(csr, s, t, bfs_scratch);
            const hbrick::ReachabilityAnswer fused_ans = fused.query(s, t);
            const hbrick::ReachabilityAnswer skip_ans = skip.query(s, t);
            ASSERT_EQ(fused_ans, expected) << "fused s=" << s << " t=" << t;
            ASSERT_EQ(skip_ans, expected) << "skip s=" << s << " t=" << t;
        }
    }
}

TEST(HBrickSkipLiftBaseline, ComposedMatrixMatchesSequentialHops) {
    hbrick::MazeLayout layout(16U, 16U);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const auto config = configFor(
        hbrick::TileSize{4U, 4U},
        hbrick::GroupSize{2U, 2U},
        hbrick::kHBrickFullDepth
    );

    hbrick::HBrickFusedLiftBaseline fused;
    fused.preprocess(graph, layout, config);
    ASSERT_EQ(fused.status(), hbrick::BaselineStatus::Completed);

    hbrick::HBrickSkipLiftBaseline skip;
    skip.adoptPrebuiltIndex(hbrick::HBrickIndex::build(graph, layout, config), &graph);
    ASSERT_EQ(skip.status(), hbrick::BaselineStatus::Completed);

    const hbrick::HBrickIndex& index = fused.index();
    ASSERT_GE(index.hierarchy().numLevels(), 3U);

    bool compared_any = false;
    const uint32_t num_tiles =
        static_cast<uint32_t>(index.brickIndex().tiles().summaries().size());
    for (uint32_t tile = 0U; tile < num_tiles; ++tile) {
        const auto chain = index.hierarchy().ancestorChain(tile);
        if (chain.size() < 3U) {
            continue;
        }
        const hbrick::SkipTileLifts* tile_lifts = skip.skipLifts().tileLifts(tile);
        ASSERT_NE(tile_lifts, nullptr);
        ASSERT_GE(tile_lifts->by_level.size(), 3U);
        ASSERT_GT(tile_lifts->by_level[2U].forward.numRows(), 0U);

        hbrick::HBrickQueryScratch scratch;
        scratch.prepare(index);
        const uint32_t num_ports = index.brickIndex().tiles().summaryByIndex(tile).numPorts();
        for (uint32_t port = 0U; port < num_ports; ++port) {
            scratch.clearLeafAndWorkspace();
            scratch.sourceChain()[0U].set(port);
            if (!scratch.sourceChain()[0U].any()) {
                continue;
            }

            fusedLiftOneStep(
                index,
                fused.fusedLifts(),
                chain,
                0U,
                true,
                scratch.sourceChain()[0U],
                scratch.sourceChain()[1U]
            );
            fusedLiftOneStep(
                index,
                fused.fusedLifts(),
                chain,
                1U,
                true,
                scratch.sourceChain()[1U],
                scratch.sourceChain()[2U]
            );

            hbrick::BitVector skip_vec(scratch.sourceChain()[2U].numBits());
            skip.skipLifts().liftSource(tile, 2U, scratch.sourceChain()[0U], skip_vec);
            ASSERT_TRUE(sameBits(scratch.sourceChain()[2U], skip_vec))
                << "tile=" << tile << " port=" << port;
            compared_any = true;
        }
    }
    EXPECT_TRUE(compared_any);
}

TEST(HBrickSkipLiftBaseline, AdaptiveM4rmMatchesRowProductOnPartialBlocks) {
    hbrick::MazeLayout layout(256U, 256U);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        layout,
        hbrick::GridEdgeConversionMode::BidirectionalAll
    );
    const auto config = configFor(
        hbrick::TileSize{32U, 32U},
        hbrick::GroupSize{4U, 4U},
        hbrick::kHBrickFullDepth
    );

    const hbrick::HBrickIndex index = hbrick::HBrickIndex::build(graph, layout, config);
    ASSERT_EQ(index.status(), hbrick::BaselineStatus::Completed);
    const hbrick::FusedTreeLifts fused = hbrick::FusedTreeLifts::build(index);
    ASSERT_TRUE(fused.isValid());
    const hbrick::SkipLevelLifts skip = hbrick::SkipLevelLifts::build(index, fused);
    ASSERT_TRUE(skip.isValid());

    bool exercised_m4rm = false;
    const uint32_t num_tiles =
        static_cast<uint32_t>(index.brickIndex().tiles().summaries().size());
    for (uint32_t tile = 0U; tile < num_tiles; ++tile) {
        const auto chain = index.hierarchy().ancestorChain(tile);
        if (chain.size() < 3U) {
            continue;
        }

        const hbrick::RegionNode& base_node =
            index.hierarchy().node(chain[0U].level, chain[0U].index);
        const hbrick::SuperTileSummary& first_parent =
            index.superSummary(chain[1U].level, chain[1U].index);
        const uint32_t first_embedding = embeddingIndexForTest(base_node, first_parent);
        if (first_embedding == UINT32_MAX) {
            continue;
        }
        const hbrick::FusedChildLift* first = fused.childLift(
            chain[1U].level,
            chain[1U].index,
            first_embedding
        );

        const hbrick::RegionNode& first_parent_node =
            index.hierarchy().node(chain[1U].level, chain[1U].index);
        const hbrick::SuperTileSummary& second_parent =
            index.superSummary(chain[2U].level, chain[2U].index);
        const uint32_t second_embedding =
            embeddingIndexForTest(first_parent_node, second_parent);
        if (second_embedding == UINT32_MAX) {
            continue;
        }
        const hbrick::FusedChildLift* second = fused.childLift(
            chain[2U].level,
            chain[2U].index,
            second_embedding
        );
        if (first == nullptr || second == nullptr
            || !adaptiveM4rmEligibleForTest(first->forward, second->forward)) {
            continue;
        }

        const hbrick::SkipTileLifts* actual = skip.tileLifts(tile);
        ASSERT_NE(actual, nullptr);
        ASSERT_GT(actual->by_level.size(), 2U);
        const hbrick::BitMatrix expected_forward =
            rowProductForTest(first->forward, second->forward);
        const hbrick::BitMatrix expected_reverse =
            rowProductForTest(first->reverse_transpose, second->reverse_transpose);
        EXPECT_TRUE(sameMatrix(expected_forward, actual->by_level[2U].forward));
        EXPECT_TRUE(sameMatrix(expected_reverse, actual->by_level[2U].reverse_transpose));
        exercised_m4rm = true;
        break;
    }
    EXPECT_TRUE(exercised_m4rm);
}
