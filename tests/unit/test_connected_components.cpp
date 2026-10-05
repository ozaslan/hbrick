#include <gtest/gtest.h>

#include "hbrick/graph/connected_components.hpp"
#include "hbrick/graph/csr_graph_builder.hpp"

TEST(ConnectedComponents, LargestComponentOnDisconnectedGraph) {
    hbrick::CsrGraphBuilder builder{6U};
    builder.addEdge(0U, 1U);
    builder.addEdge(2U, 3U);
    builder.addEdge(3U, 4U);
    builder.addEdge(4U, 5U);
    const hbrick::CsrGraph graph = builder.build();

    EXPECT_EQ(hbrick::largestUndirectedComponentSize(graph), 4U);
}

TEST(ConnectedComponents, FillLabelsOnDisconnectedGraph) {
    hbrick::CsrGraphBuilder builder{6U};
    builder.addEdge(0U, 1U);
    builder.addEdge(2U, 3U);
    builder.addEdge(3U, 4U);
    builder.addEdge(4U, 5U);
    const hbrick::CsrGraph graph = builder.build();

    std::vector<uint32_t> labels(6U);
    EXPECT_EQ(hbrick::fillUndirectedComponentLabels(graph, labels), 4U);
    EXPECT_NE(labels[0U], labels[2U]);
    EXPECT_EQ(labels[0U], labels[1U]);
    EXPECT_EQ(labels[2U], labels[3U]);
    EXPECT_EQ(labels[3U], labels[4U]);
    EXPECT_EQ(labels[4U], labels[5U]);
}

TEST(ConnectedComponents, SingleVertexComponent) {
    hbrick::CsrGraphBuilder builder{1U};
    const hbrick::CsrGraph graph = builder.build();

    EXPECT_EQ(hbrick::largestUndirectedComponentSize(graph), 1U);
}

TEST(ConnectedComponents, UndirectedLinkUsesReverseReachability) {
    hbrick::CsrGraphBuilder builder{3U};
    builder.addEdge(0U, 1U);
    const hbrick::CsrGraph graph = builder.build();

    EXPECT_EQ(hbrick::largestUndirectedComponentSize(graph), 2U);
}
