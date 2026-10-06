#include <gtest/gtest.h>

#include "options.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace muckaii::tui;

namespace
{
std::optional<Options> parse(std::vector<std::string> words, std::string& error)
{
    words.insert(words.begin(), "muckaiiTui");
    std::vector<char*> args;
    for (std::string& word : words)
        args.push_back(word.data());
    return parseArguments(args, error);
}
} // namespace

TEST(Options, PasswordFlagIsTakenAndComesFirst)
{
    std::string                  error;
    const std::optional<Options> options { parse({ "--host", "h", "--account", "Z1", "--password", "pw-1" },
                                                 error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_EQ(options->mPassword, std::optional<std::string> { "pw-1" });
    EXPECT_EQ(findGivenPassword(*options, std::string { "fromEnv" }), std::optional<std::string> { "pw-1" });
}

TEST(Options, TheEnvironmentComesNextThenNothing)
{
    std::string                  error;
    const std::optional<Options> options { parse({ "--host", "h" }, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_EQ(findGivenPassword(*options, std::string { "fromEnv" }),
              std::optional<std::string> { "fromEnv" });
    EXPECT_FALSE(findGivenPassword(*options, std::nullopt).has_value());
}

TEST(Options, APasswordMudCannotReceiveIsRefused)
{
    std::string error;
    EXPECT_FALSE(parse({ "--host", "h", "--password", "a\tb" }, error).has_value());
    EXPECT_NE(error.find("printable 7-bit"), std::string::npos);
    EXPECT_FALSE(parse({ "--host", "h", "--password", "caf\xC3\xA9" }, error).has_value());
    EXPECT_FALSE(parse({ "--host", "h", "--account", "Z1\r" }, error).has_value());
}

// A value that looks like a flag but is not a known one is a value: a password may start with '-'.
TEST(Options, APasswordMayStartWithADash)
{
    std::string                  error;
    const std::optional<Options> options { parse({ "--host", "h", "--password", "-secret" }, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_EQ(options->mPassword, std::optional<std::string> { "-secret" });
    EXPECT_FALSE(parse({ "--password", "--host", "h" }, error).has_value());
    EXPECT_NE(error.find("missing value"), std::string::npos);
}
