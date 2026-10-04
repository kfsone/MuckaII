#include "muckaii/audio/audio.hpp"

#include <gtest/gtest.h>

TEST(AudioLayer, NamesItself)
{
    EXPECT_EQ(muckaii::audio::getLayerName(), "audio");
}
