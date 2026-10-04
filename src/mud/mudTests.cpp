#include "muckaii/mud/mud.hpp"

#include <gtest/gtest.h>

TEST(MudLayer, NamesItself)
{
    EXPECT_EQ(muckaii::mud::getLayerName(), "mud");
}
