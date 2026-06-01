#include <gtest/gtest.h>

#include "hbrick/graph/directed_grid_graph_builder.hpp"
#include "hbrick/grid/passable_grid.hpp"
#include "hbrick/viz/grid_graph_renderer.hpp"
#include "hbrick/viz/svg_canvas.hpp"

TEST(SvgCanvas, PlainTextRemainsPlain) {
    hbrick::SvgCanvas canvas{100U, 50U};
    canvas.text(10.0, 20.0, "plain");

    const std::string svg = canvas.toString();
    EXPECT_NE(svg.find(">plain</text>"), std::string::npos);
}

TEST(SvgCanvas, EscapesAmpersand) {
    hbrick::SvgCanvas canvas{100U, 50U};
    canvas.text(0.0, 0.0, "a & b");

    const std::string svg = canvas.toString();
    EXPECT_NE(svg.find(">a &amp; b</text>"), std::string::npos);
    EXPECT_EQ(svg.find("a & b</text>"), std::string::npos);
}

TEST(SvgCanvas, EscapesLessThanAndGreaterThan) {
    hbrick::SvgCanvas canvas{100U, 50U};
    canvas.text(0.0, 0.0, "x < y");
    canvas.text(0.0, 10.0, "x > y");

    const std::string svg = canvas.toString();
    EXPECT_NE(svg.find(">x &lt; y</text>"), std::string::npos);
    EXPECT_NE(svg.find(">x &gt; y</text>"), std::string::npos);
}

TEST(SvgCanvas, EscapesQuoteAndApostrophe) {
    hbrick::SvgCanvas canvas{100U, 50U};
    canvas.text(0.0, 0.0, R"(say "hi")");
    canvas.text(0.0, 10.0, "it's fine");

    const std::string svg = canvas.toString();
    EXPECT_NE(svg.find("&quot;"), std::string::npos);
    EXPECT_NE(svg.find("&apos;"), std::string::npos);
}

TEST(SvgCanvas, ToStringProducesSvgRoot) {
    hbrick::SvgCanvas canvas{120U, 80U};
    canvas.rect(0.0, 0.0, 10.0, 10.0, "#ff0000");

    const std::string svg = canvas.toString();
    EXPECT_NE(svg.find("<svg xmlns=\"http://www.w3.org/2000/svg\""), std::string::npos);
    EXPECT_NE(svg.find("width=\"120\""), std::string::npos);
    EXPECT_NE(svg.find("height=\"80\""), std::string::npos);
    EXPECT_NE(svg.find("<rect"), std::string::npos);
    EXPECT_NE(svg.find("</svg>"), std::string::npos);
}

TEST(GridGraphRenderer, RendersGridCellsAndEdges) {
    const hbrick::PassableGrid grid(2U, 2U);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::AcyclicEastSouth
    );

    const hbrick::SvgCanvas canvas = hbrick::GridGraphRenderer::render(grid, graph, 20.0);
    const std::string svg = canvas.toString();

    EXPECT_EQ(canvas.widthPixels(), 40U);
    EXPECT_EQ(canvas.heightPixels(), 40U);
    EXPECT_NE(svg.find("<rect"), std::string::npos);
    EXPECT_NE(svg.find("<line"), std::string::npos);
    EXPECT_NE(svg.find("H-BRICK grid graph"), std::string::npos);
}

TEST(GridGraphRenderer, EscapesUnsafeLabelsInRenderedOutput) {
    hbrick::PassableGrid grid(1U, 1U);
    const hbrick::DirectedGridGraph graph = hbrick::DirectedGridGraphBuilder::build(
        grid,
        hbrick::GridEdgeConversionMode::AcyclicEastSouth
    );

    hbrick::SvgCanvas canvas = hbrick::GridGraphRenderer::render(grid, graph, 20.0);
    canvas.text(0.0, 15.0, "a & b < c");

    const std::string svg = canvas.toString();
    EXPECT_NE(svg.find("a &amp; b &lt; c"), std::string::npos);
}
