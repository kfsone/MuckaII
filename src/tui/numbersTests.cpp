#include <gtest/gtest.h>

#include "numbers.hpp"

namespace muckaii::tui
{
namespace
{
// NumbersTest_ParseIntAcceptsOnlyAWholeNumber checks that decimal parsing accepts only a complete integer.
TEST(NumbersTest, ParseIntAcceptsOnlyAWholeNumber)
{
    EXPECT_EQ(parseInt("0"), types::OptInt { 0 });
    EXPECT_EQ(parseInt("160"), types::OptInt { 160 });
    EXPECT_EQ(parseInt("-1"), types::OptInt { -1 });
    EXPECT_FALSE(parseInt("").has_value());
    EXPECT_FALSE(parseInt("20abc").has_value());
    EXPECT_FALSE(parseInt(" 20").has_value());
    EXPECT_FALSE(parseInt("+20").has_value());
}

// NumbersTest_ParseIntRejectsOverflow checks that integers outside the supported range are rejected.
TEST(NumbersTest, ParseIntRejectsOverflow)
{
    EXPECT_FALSE(parseInt("99999999999").has_value());
}

// NumbersTest_IsValidPortBounds checks that only ports within the usable TCP range are accepted.
TEST(NumbersTest, IsValidPortBounds)
{
    EXPECT_FALSE(isValidPort(0));
    EXPECT_TRUE(isValidPort(1));
    EXPECT_TRUE(isValidPort(kDefaultPort));
    EXPECT_TRUE(isValidPort(65535));
    EXPECT_FALSE(isValidPort(65536));
    EXPECT_FALSE(isValidPort(-1));
}
} // namespace
} // namespace muckaii::tui
