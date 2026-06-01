#include <gtest/gtest.h>

#include "hbrick/core/grid_coord.hpp"
#include "hbrick/core/reachability_query.hpp"
#include "hbrick/core/status_reporting.hpp"
#include "hbrick/core/types.hpp"
#include "hbrick/core/vertex_id.hpp"

TEST(CoreTypes, ReachabilityAnswerValues) {
    EXPECT_EQ(static_cast<uint8_t>(hbrick::ReachabilityAnswer::Unreachable), 0U);
    EXPECT_EQ(static_cast<uint8_t>(hbrick::ReachabilityAnswer::Reachable), 1U);
}

TEST(CoreTypes, BaselineStatusValues) {
    EXPECT_EQ(static_cast<uint8_t>(hbrick::BaselineStatus::NotRun), 0U);
    EXPECT_EQ(static_cast<uint8_t>(hbrick::BaselineStatus::Completed), 1U);
    EXPECT_EQ(static_cast<uint8_t>(hbrick::BaselineStatus::SkippedByPolicy), 2U);
    EXPECT_EQ(static_cast<uint8_t>(hbrick::BaselineStatus::OutOfMemory), 3U);
    EXPECT_EQ(static_cast<uint8_t>(hbrick::BaselineStatus::Failed), 4U);
}

TEST(CoreTypes, VertexIdValueAndValidity) {
    const hbrick::VertexId vertex{42U};
    EXPECT_EQ(vertex.value, 42U);
    EXPECT_TRUE(vertex.isValid());

    const hbrick::VertexId invalid = hbrick::VertexId::invalid();
    EXPECT_FALSE(invalid.isValid());
    EXPECT_EQ(invalid.value, hbrick::kInvalidVertexId);
}

TEST(CoreTypes, VertexIdEquality) {
    EXPECT_EQ(hbrick::VertexId{7U}, hbrick::VertexId{7U});
    EXPECT_NE(hbrick::VertexId{7U}, hbrick::VertexId{8U});
}

TEST(CoreTypes, GridCoordLinearIndexIsRowMajor) {
    const hbrick::GridCoord coord{2U, 3U};
    EXPECT_EQ(coord.linearIndex(10U), 32U);

    const hbrick::GridCoord origin{0U, 0U};
    EXPECT_EQ(origin.linearIndex(5U), 0U);

    const hbrick::GridCoord east_of_origin{4U, 1U};
    EXPECT_EQ(east_of_origin.linearIndex(5U), 9U);
}

TEST(CoreTypes, ReachabilityQueryUsesVertexIdBoundary) {
    const hbrick::ReachabilityQuery query{hbrick::VertexId{1U}, hbrick::VertexId{9U}};

    EXPECT_TRUE(query.isValid());
    EXPECT_EQ(query.source.value, 1U);
    EXPECT_EQ(query.target.value, 9U);

    const uint32_t source = query.source.value;
    const uint32_t target = query.target.value;
    EXPECT_EQ(source, 1U);
    EXPECT_EQ(target, 9U);
}

TEST(CoreTypes, ReachabilityQueryInvalidWhenVertexMissing) {
    const hbrick::ReachabilityQuery query{
        hbrick::VertexId{0U},
        hbrick::VertexId::invalid(),
    };
    EXPECT_FALSE(query.isValid());
}

TEST(CoreTypes, StatusReportingStrings) {
    EXPECT_EQ(hbrick::toString(hbrick::ReachabilityAnswer::Unreachable), "Unreachable");
    EXPECT_EQ(hbrick::toString(hbrick::ReachabilityAnswer::Reachable), "Reachable");

    EXPECT_EQ(hbrick::toString(hbrick::BaselineStatus::NotRun), "NotRun");
    EXPECT_EQ(hbrick::toString(hbrick::BaselineStatus::Completed), "Completed");
    EXPECT_EQ(hbrick::toString(hbrick::BaselineStatus::SkippedByPolicy), "SkippedByPolicy");
    EXPECT_EQ(hbrick::toString(hbrick::BaselineStatus::OutOfMemory), "OutOfMemory");
    EXPECT_EQ(hbrick::toString(hbrick::BaselineStatus::Failed), "Failed");
}
