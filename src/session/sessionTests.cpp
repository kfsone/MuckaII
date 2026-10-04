#include "muckaii/session/session.hpp"

#include <gtest/gtest.h>

TEST(SessionLayer, NamesItself)
{
    EXPECT_EQ(muckaii::session::getLayerName(), "session");
}
