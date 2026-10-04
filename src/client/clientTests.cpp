#include "muckaii/client/client.hpp"

#include <gtest/gtest.h>

using muckaii::client::parseCount;

TEST(ClientParseCount, AcceptsADecimalNumber)
{
    int count { 0 };
    EXPECT_EQ(parseCount("42", count), std::errc {});
    EXPECT_EQ(count, 42);
}

TEST(ClientParseCount, RejectsTrailingText)
{
    int count { 7 };
    EXPECT_EQ(parseCount("42x", count), std::errc::invalid_argument);
    EXPECT_EQ(count, 7);
}

TEST(ClientParseCount, RejectsEmptyInput)
{
    int count { 7 };
    EXPECT_EQ(parseCount("", count), std::errc::invalid_argument);
    EXPECT_EQ(count, 7);
}

TEST(ClientParseCount, ReportsOutOfRange)
{
    int count { 7 };
    EXPECT_EQ(parseCount("99999999999999999999", count), std::errc::result_out_of_range);
    EXPECT_EQ(count, 7);
}
