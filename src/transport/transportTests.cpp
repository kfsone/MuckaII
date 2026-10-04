#include "muckaii/transport/transport.hpp"

#include <gtest/gtest.h>

TEST(TransportLayer, NamesItself)
{
    EXPECT_EQ(muckaii::transport::getLayerName(), "transport");
}
