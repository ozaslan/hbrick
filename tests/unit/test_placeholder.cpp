#include <gtest/gtest.h>

#include "hbrick/core/version.hpp"

TEST(Stage0, PlaceholderBuildsAndLinksCore) {
    EXPECT_STREQ(hbrick::libraryVersion(), hbrick::kLibraryVersion.data());
}
