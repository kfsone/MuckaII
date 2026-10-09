#include "muckaii/base/types.hpp"

#include <gtest/gtest.h>

#include "options.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace muckaii::tui;
namespace types = muckaii::types;

namespace
{
// parse builds mutable argv storage and delegates to the command-line parser.
std::optional<Options> parse(std::vector<std::string> words, std::string& error)
{
    words.insert(words.begin(), "muckaiiTui");
    std::vector<char*> args;
    for (std::string& word : words)
        args.push_back(word.data());
    return parseArguments(args, error);
}
} // namespace

// Options_PasswordFlagIsTakenAndComesFirst checks that --password takes precedence over the environment value.
TEST(Options, PasswordFlagIsTakenAndComesFirst)
{
    std::string                  error;
    std::vector<std::string>     words { "--host", "h", "--account", "Z1", "--password", "pw-1" };
    const std::optional<Options> options { parse(std::move(words), error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_EQ(options->mPassword, types::OptString { "pw-1" });
    EXPECT_EQ(findGivenPassword(*options, std::string { "fromEnv" }), types::OptString { "pw-1" });
}

// Options_TheEnvironmentComesNextThenNothing checks that the environment supplies a password only when the flag is absent.
TEST(Options, TheEnvironmentComesNextThenNothing)
{
    std::string                  error;
    const std::optional<Options> options { parse({ "--host", "h" }, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_EQ(findGivenPassword(*options, std::string { "fromEnv" }), types::OptString { "fromEnv" });
    EXPECT_FALSE(findGivenPassword(*options, std::nullopt).has_value());
}

// Options_APasswordMudCannotReceiveIsRefused checks that credentials containing non-printable 7-bit text are refused.
TEST(Options, APasswordMudCannotReceiveIsRefused)
{
    std::string error;
    EXPECT_FALSE(parse({ "--host", "h", "--password", "a\tb" }, error).has_value());
    EXPECT_NE(error.find("printable 7-bit"), std::string::npos);
    EXPECT_FALSE(parse({ "--host", "h", "--password", "caf\xC3\xA9" }, error).has_value());
    EXPECT_FALSE(parse({ "--host", "h", "--account", "Z1\r" }, error).has_value());
}

// Options_APasswordMayStartWithADash checks that an unknown dash-prefixed word remains a valid value.
TEST(Options, APasswordMayStartWithADash)
{
    std::string                  error;
    const std::optional<Options> options { parse({ "--host", "h", "--password", "-secret" }, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_EQ(options->mPassword, types::OptString { "-secret" });
    EXPECT_FALSE(parse({ "--password", "--host", "h" }, error).has_value());
    EXPECT_NE(error.find("missing value"), std::string::npos);
}

// Options_NoArgumentsAtAllIsNotDirectConnectAndHostIsNoLongerRequired checks that no flags select the Profile List without requiring a host.
TEST(Options, NoArgumentsAtAllIsNotDirectConnectAndHostIsNoLongerRequired)
{
    std::string                  error;
    const std::optional<Options> options { parse({}, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_FALSE(options->mIsDirectConnect);
    EXPECT_TRUE(options->mHost.empty());
}

// Options_HelpAloneIsNotDirectConnect checks that --help does not select direct-connect mode.
TEST(Options, HelpAloneIsNotDirectConnect)
{
    std::string                  error;
    const std::optional<Options> options { parse({ "--help" }, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_TRUE(options->mIsHelp);
    EXPECT_FALSE(options->mIsDirectConnect);
}

// Options_ProfileFlagSelectsDirectConnectMode checks that --profile selects direct-connect mode.
TEST(Options, ProfileFlagSelectsDirectConnectMode)
{
    std::string                  error;
    const std::optional<Options> options { parse({ "--profile", "Home" }, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_TRUE(options->mIsDirectConnect);
    EXPECT_EQ(options->mProfileName, types::OptString { "Home" });
}

// Options_HostFlagAloneSelectsDirectConnectMode checks that --host alone selects direct-connect mode.
TEST(Options, HostFlagAloneSelectsDirectConnectMode)
{
    std::string                  error;
    const std::optional<Options> options { parse({ "--host", "h" }, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_TRUE(options->mIsDirectConnect);
}

// Options_PortFlagAloneSelectsDirectConnectModeEvenWithoutHost checks that --port selects direct-connect mode without requiring --host.
TEST(Options, PortFlagAloneSelectsDirectConnectModeEvenWithoutHost)
{
    std::string                  error;
    const std::optional<Options> options { parse({ "--port", "6023" }, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_TRUE(options->mIsDirectConnect);
    EXPECT_TRUE(options->mHost.empty());
    EXPECT_EQ(options->mPort, 6023);
}

// Options_AnOutOfRangePortStillSelectsDirectConnectModeButIsIgnored checks that an invalid port is ignored without changing direct-connect mode.
TEST(Options, AnOutOfRangePortStillSelectsDirectConnectModeButIsIgnored)
{
    std::string                  error;
    const std::optional<Options> options { parse({ "--port", "99999" }, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_TRUE(options->mIsDirectConnect); // the flag was given, even though its value is ignored
    EXPECT_EQ(options->mPort, kDefaultPort);
}

// Options_UserFlagOverridesTheLoginNameAndSelectsDirectConnectMode checks that --user overrides the login name and selects direct-connect mode.
TEST(Options, UserFlagOverridesTheLoginNameAndSelectsDirectConnectMode)
{
    std::string                  error;
    const std::optional<Options> options { parse({ "--user", "mudx" }, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_TRUE(options->mIsDirectConnect);
    EXPECT_EQ(options->mLoginName, types::OptString { "mudx" });
}

// Options_ALoginNameMudCannotReceiveIsRefused checks that login names containing non-printable text are refused.
TEST(Options, ALoginNameMudCannotReceiveIsRefused)
{
    std::string error;
    EXPECT_FALSE(parse({ "--user", "bad\tname" }, error).has_value());
    EXPECT_NE(error.find("printable 7-bit"), std::string::npos);
}

// Options_MaxColumnsAndWireLogAloneDoNotSelectDirectConnectMode checks that --max-columns and --wire-log alone leave Profile List mode selected.
TEST(Options, MaxColumnsAndWireLogAloneDoNotSelectDirectConnectMode)
{
    std::string                  error;
    const std::optional<Options> options { parse({ "--max-columns", "100", "--wire-log", "x.db" }, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_FALSE(options->mIsDirectConnect);
}
