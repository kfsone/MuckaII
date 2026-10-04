#include "muckaii/core/core.hpp"

#include <gtest/gtest.h>

TEST(CoreLayer, NamesItself)
{
    EXPECT_EQ(muckaii::core::getLayerName(), "core");
}
