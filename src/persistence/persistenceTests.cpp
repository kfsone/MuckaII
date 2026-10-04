#include "muckaii/persistence/persistence.hpp"

#include <gtest/gtest.h>

TEST(PersistenceLayer, NamesItself)
{
    EXPECT_EQ(muckaii::persistence::getLayerName(), "persistence");
}
