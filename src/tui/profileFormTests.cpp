#include "muckaii/base/types.hpp"

#include <gtest/gtest.h>

#include "profileForm.hpp"

namespace muckaii::tui
{
namespace
{
// ProfileFormTest_CycleLoginNameStepsForwardAndWrapsAtTheEnd checks that login cycling advances through every supported name and wraps forward.
TEST(ProfileFormTest, CycleLoginNameStepsForwardAndWrapsAtTheEnd)
{
    EXPECT_EQ(cycleLoginName("mud", 1), "mudx");
    EXPECT_EQ(cycleLoginName("mudx", 1), "mudguest");
    EXPECT_EQ(cycleLoginName("mudguest", 1), "mudguestx");
    EXPECT_EQ(cycleLoginName("mudguestx", 1), "mud");
}

// ProfileFormTest_CycleLoginNameStepsBackwardAndWrapsAtTheStart checks that login cycling moves backward and wraps at the first name.
TEST(ProfileFormTest, CycleLoginNameStepsBackwardAndWrapsAtTheStart)
{
    EXPECT_EQ(cycleLoginName("mud", -1), "mudguestx");
    EXPECT_EQ(cycleLoginName("mudguestx", -1), "mudguest");
}

// ProfileFormTest_CycleLoginNameTreatsAnUnknownNameAsTheFirstEntry checks that an unsupported current login cycles from the first entry.
TEST(ProfileFormTest, CycleLoginNameTreatsAnUnknownNameAsTheFirstEntry)
{
    // A hand-edited or pre-feature profile might hold a login name outside the fixed set.
    EXPECT_EQ(cycleLoginName("somethingElse", 1), "mudx");
    EXPECT_EQ(cycleLoginName("somethingElse", 0), "mud");
}

// ProfileFormTest_CycleLoginNameAcceptsAStepLargerThanTheListSize checks that login cycling reduces large steps modulo the list size.
TEST(ProfileFormTest, CycleLoginNameAcceptsAStepLargerThanTheListSize)
{
    EXPECT_EQ(cycleLoginName("mud", 5), cycleLoginName("mud", 1)); // 5 % 4 == 1
}

// ProfileFormTest_SplitHostPortWithNoColonLeavesPortUnset checks that a host without a colon leaves the port unset.
TEST(ProfileFormTest, SplitHostPortWithNoColonLeavesPortUnset)
{
    const HostPort result { splitHostPort("mud2.co.uk") };
    EXPECT_EQ(result.mHost, "mud2.co.uk");
    EXPECT_FALSE(result.mPort.has_value());
}

// ProfileFormTest_SplitHostPortWithAColonAndANumberSplitsBoth checks that a numeric suffix splits into host and port.
TEST(ProfileFormTest, SplitHostPortWithAColonAndANumberSplitsBoth)
{
    const HostPort result { splitHostPort("mud2.co.uk:6023") };
    EXPECT_EQ(result.mHost, "mud2.co.uk");
    ASSERT_TRUE(result.mPort.has_value());
    EXPECT_EQ(*result.mPort, 6023);
}

// ProfileFormTest_SplitHostPortWithANonNumericSuffixKeepsItPartOfTheHost checks that a nonnumeric suffix remains part of the host.
TEST(ProfileFormTest, SplitHostPortWithANonNumericSuffixKeepsItPartOfTheHost)
{
    // A colon that is not a port separator (no reserved syntax to tell them apart) keeps the whole
    // text as the host and leaves the port unset, rather than silently truncating the host.
    const HostPort result { splitHostPort("mud2.co.uk:not-a-port") };
    EXPECT_EQ(result.mHost, "mud2.co.uk:not-a-port");
    EXPECT_FALSE(result.mPort.has_value());
}

// ProfileFormTest_SplitHostPortTrimsSurroundingWhitespace checks that host and port parsing trims surrounding whitespace.
TEST(ProfileFormTest, SplitHostPortTrimsSurroundingWhitespace)
{
    const HostPort result { splitHostPort("  mud2.co.uk:23  ") };
    EXPECT_EQ(result.mHost, "mud2.co.uk");
    ASSERT_TRUE(result.mPort.has_value());
    EXPECT_EQ(*result.mPort, 23);
}

// ProfileFormTest_SplitHostPortWithOnlyAColonLeavesTheHostEmpty checks that a valid port with no host leaves the host empty.
TEST(ProfileFormTest, SplitHostPortWithOnlyAColonLeavesTheHostEmpty)
{
    const HostPort result { splitHostPort(":23") };
    EXPECT_TRUE(result.mHost.empty());
}

// ProfileFormTest_JoinHostPortOmitsTheDefaultPort checks that the default port is omitted from the displayed endpoint.
TEST(ProfileFormTest, JoinHostPortOmitsTheDefaultPort)
{
    EXPECT_EQ(joinHostPort("mud2.co.uk", 23), "mud2.co.uk");
}

// ProfileFormTest_JoinHostPortShowsAnyOtherPort checks that a non-default port is included in the displayed endpoint.
TEST(ProfileFormTest, JoinHostPortShowsAnyOtherPort)
{
    EXPECT_EQ(joinHostPort("mud2.co.uk", 6023), "mud2.co.uk:6023");
}

// ProfileFormTest_FormatMaxColumnsTextIsBlankWhenUnset checks that an unset width displays as blank text.
TEST(ProfileFormTest, FormatMaxColumnsTextIsBlankWhenUnset)
{
    EXPECT_TRUE(formatMaxColumnsText(std::nullopt).empty());
}

// ProfileFormTest_FormatMaxColumnsTextShowsZeroAndAnExplicitValue checks that zero and explicit widths display as decimal text.
TEST(ProfileFormTest, FormatMaxColumnsTextShowsZeroAndAnExplicitValue)
{
    EXPECT_EQ(formatMaxColumnsText(0), "0");
    EXPECT_EQ(formatMaxColumnsText(100), "100");
}

// ProfileFormTest_ParseMaxColumnsTextAcceptsBlankZeroAndTheFullRange checks that blank, client-wrap zero and both width limits are accepted.
TEST(ProfileFormTest, ParseMaxColumnsTextAcceptsBlankZeroAndTheFullRange)
{
    EXPECT_TRUE(parseMaxColumnsText("").isValid());
    EXPECT_FALSE(parseMaxColumnsText("").mMaxColumns.has_value());
    EXPECT_FALSE(parseMaxColumnsText("   ").mMaxColumns.has_value());
    EXPECT_EQ(parseMaxColumnsText("0").mMaxColumns, types::OptInt { 0 });
    EXPECT_EQ(parseMaxColumnsText("20").mMaxColumns, types::OptInt { 20 });
    EXPECT_EQ(parseMaxColumnsText("160").mMaxColumns, types::OptInt { 160 });
}

// ProfileFormTest_ParseMaxColumnsTextRejectsOutOfRangeAndNonNumericText checks that out-of-range and nonnumeric width values are rejected.
TEST(ProfileFormTest, ParseMaxColumnsTextRejectsOutOfRangeAndNonNumericText)
{
    for (const std::string_view text : { "19", "1", "161", "-1", "abc", "20abc", "1e2" })
    {
        const MaxColumnsParse parsed { parseMaxColumnsText(text) };
        EXPECT_FALSE(parsed.isValid()) << text;
        EXPECT_FALSE(parsed.mMaxColumns.has_value()) << text;
    }
}

// ProfileFormTest_ToFieldsRoundTripsEveryFieldOfANewProfile checks that a new profile maps to its expected editable fields.
TEST(ProfileFormTest, ToFieldsRoundTripsEveryFieldOfANewProfile)
{
    const persistence::Profile profile { makeNewProfile() };
    const ProfileFields        fields { toFields(profile) };
    EXPECT_EQ(fields.mName, "New profile");
    EXPECT_EQ(fields.mHostPort, "mud2.co.uk"); // default port 23 omitted
    EXPECT_EQ(fields.mLoginName, "mud");
    EXPECT_TRUE(fields.mAccount.empty());
    EXPECT_TRUE(fields.mPassword.empty());
    EXPECT_TRUE(fields.mPersonaName.empty());
    EXPECT_TRUE(fields.mMaxColumnsText.empty());
}

// ProfileFormTest_ToFieldsShowsANonDefaultPortAndMaxColumns checks that the form displays a non-default port and explicit width.
TEST(ProfileFormTest, ToFieldsShowsANonDefaultPortAndMaxColumns)
{
    persistence::Profile profile { makeNewProfile() };
    profile.mPort       = 6023;
    profile.mMaxColumns = 100;
    const ProfileFields fields { toFields(profile) };
    EXPECT_EQ(fields.mHostPort, "mud2.co.uk:6023");
    EXPECT_EQ(fields.mMaxColumnsText, "100");
}

// ProfileFormTest_FromFieldsBuildsAProfileKeepingTheBasesIdentityFields checks that saving fields preserves the base identity and applies editable values.
TEST(ProfileFormTest, FromFieldsBuildsAProfileKeepingTheBasesIdentityFields)
{
    persistence::Profile base { makeNewProfile() };
    base.mId                      = 42;
    base.mLastUsedMs              = 1000;
    base.mManagedPersonaSelection = false;
    ProfileFields fields { toFields(base) };
    fields.mName           = "My profile";
    fields.mHostPort       = "example.org:6023";
    fields.mAccount        = "abc123";
    fields.mPassword       = "secret";
    fields.mPersonaName    = "Gorblimey";
    fields.mMaxColumnsText = "100";

    const ProfileResult outcome { fromFields(fields, base) };
    ASSERT_TRUE(outcome.mProfile.has_value()) << outcome.mError;
    const persistence::Profile& result { *outcome.mProfile };
    EXPECT_EQ(result.mId, 42);
    EXPECT_EQ(result.mLastUsedMs, 1000);
    EXPECT_FALSE(result.mManagedPersonaSelection);
    EXPECT_EQ(result.mName, "My profile");
    EXPECT_EQ(result.mHost, "example.org");
    EXPECT_EQ(result.mPort, 6023);
    EXPECT_EQ(result.mAccount, "abc123");
    EXPECT_EQ(result.mPassword, "secret");
    EXPECT_EQ(result.mPersonaName, "Gorblimey");
    ASSERT_TRUE(result.mMaxColumns.has_value());
    EXPECT_EQ(*result.mMaxColumns, 100);
}

// ProfileFormTest_FromFieldsKeepsTheBasesPortWhenTheHostFieldHasNoPort checks that a host without a port keeps the base profile port.
TEST(ProfileFormTest, FromFieldsKeepsTheBasesPortWhenTheHostFieldHasNoPort)
{
    persistence::Profile base { makeNewProfile() };
    base.mPort = 6023;
    ProfileFields fields { toFields(base) };
    fields.mHostPort = "example.org"; // no :port this time

    const ProfileResult outcome { fromFields(fields, base) };
    ASSERT_TRUE(outcome.mProfile.has_value()) << outcome.mError;
    const persistence::Profile& result { *outcome.mProfile };
    EXPECT_EQ(result.mHost, "example.org");
    EXPECT_EQ(result.mPort, 6023);
}

// ProfileFormTest_FromFieldsRejectsABlankName checks that a blank profile name fails validation.
TEST(ProfileFormTest, FromFieldsRejectsABlankName)
{
    const persistence::Profile base { makeNewProfile() };
    ProfileFields              fields { toFields(base) };
    fields.mName = "   ";
    const ProfileResult outcome { fromFields(fields, base) };
    EXPECT_FALSE(outcome.mProfile.has_value());
    EXPECT_FALSE(outcome.mError.empty());
}

// ProfileFormTest_FromFieldsRejectsABlankHost checks that a blank host fails validation.
TEST(ProfileFormTest, FromFieldsRejectsABlankHost)
{
    const persistence::Profile base { makeNewProfile() };
    ProfileFields              fields { toFields(base) };
    fields.mHostPort = "   ";
    const ProfileResult outcome { fromFields(fields, base) };
    EXPECT_FALSE(outcome.mProfile.has_value());
    EXPECT_FALSE(outcome.mError.empty());
}

// ProfileFormTest_FromFieldsRejectsAnOutOfRangePort checks that an out-of-range port fails validation.
TEST(ProfileFormTest, FromFieldsRejectsAnOutOfRangePort)
{
    const persistence::Profile base { makeNewProfile() };
    ProfileFields              fields { toFields(base) };
    fields.mHostPort = "example.org:70000";
    const ProfileResult outcome { fromFields(fields, base) };
    EXPECT_FALSE(outcome.mProfile.has_value());
    EXPECT_FALSE(outcome.mError.empty());
}

// ProfileFormTest_FromFieldsEnforcesThePortBoundaries checks that only ports from 1 through 65535 pass validation.
TEST(ProfileFormTest, FromFieldsEnforcesThePortBoundaries)
{
    const persistence::Profile base { makeNewProfile() };
    ProfileFields              fields { toFields(base) };
    for (const std::string_view hostPort : { "example.org:0", "example.org:65536", "example.org:-1" })
    {
        fields.mHostPort = std::string { hostPort };
        EXPECT_FALSE(fromFields(fields, base).mProfile.has_value()) << hostPort;
    }
    for (const int port : { 1, 65535 })
    {
        fields.mHostPort = "example.org:" + std::to_string(port);
        const ProfileResult outcome { fromFields(fields, base) };
        ASSERT_TRUE(outcome.mProfile.has_value()) << outcome.mError;
        EXPECT_EQ(outcome.mProfile->mPort, port);
    }
}

// ProfileFormTest_FromFieldsRejectsANonPrintablePersonaAndNamesIt checks that a non-printable persona is rejected with a field-specific error.
TEST(ProfileFormTest, FromFieldsRejectsANonPrintablePersonaAndNamesIt)
{
    const persistence::Profile base { makeNewProfile() };
    ProfileFields              fields { toFields(base) };
    fields.mPersonaName = "bad\x7f";
    const ProfileResult outcome { fromFields(fields, base) };
    EXPECT_FALSE(outcome.mProfile.has_value());
    EXPECT_NE(outcome.mError.find("Persona"), std::string::npos);
}

// ProfileFormTest_FromFieldsRejectsANonPrintableAccount checks that a non-printable account is rejected.
TEST(ProfileFormTest, FromFieldsRejectsANonPrintableAccount)
{
    const persistence::Profile base { makeNewProfile() };
    ProfileFields              fields { toFields(base) };
    fields.mAccount = "bad\x01name";
    const ProfileResult outcome { fromFields(fields, base) };
    EXPECT_FALSE(outcome.mProfile.has_value());
    EXPECT_FALSE(outcome.mError.empty());
}

// ProfileFormTest_FromFieldsRejectsANonPrintablePassword checks that a non-printable password is rejected.
TEST(ProfileFormTest, FromFieldsRejectsANonPrintablePassword)
{
    const persistence::Profile base { makeNewProfile() };
    ProfileFields              fields { toFields(base) };
    fields.mPassword = "bad\nvalue";
    const ProfileResult outcome { fromFields(fields, base) };
    EXPECT_FALSE(outcome.mProfile.has_value());
    EXPECT_FALSE(outcome.mError.empty());
}

// ProfileFormTest_FromFieldsRejectsAnOutOfRangeMaxColumnsText checks that an invalid Terminal Width value is rejected.
TEST(ProfileFormTest, FromFieldsRejectsAnOutOfRangeMaxColumnsText)
{
    const persistence::Profile base { makeNewProfile() };
    ProfileFields              fields { toFields(base) };
    fields.mMaxColumnsText = "5";
    const ProfileResult outcome { fromFields(fields, base) };
    EXPECT_FALSE(outcome.mProfile.has_value());
    EXPECT_FALSE(outcome.mError.empty());
}

// ProfileFormTest_FromFieldsAcceptsEmptyOptionalFields checks that empty optional fields remain valid.
TEST(ProfileFormTest, FromFieldsAcceptsEmptyOptionalFields)
{
    const persistence::Profile base { makeNewProfile() };
    ProfileFields              fields { toFields(base) };
    fields.mName     = "Minimal";
    fields.mHostPort = "mud2.co.uk";
    // mAccount, mPassword, mPersonaName and mMaxColumnsText are all left blank.
    const ProfileResult outcome { fromFields(fields, base) };
    ASSERT_TRUE(outcome.mProfile.has_value()) << outcome.mError;
    const persistence::Profile& result { *outcome.mProfile };
    EXPECT_TRUE(result.mAccount.empty());
    EXPECT_TRUE(result.mPassword.empty());
    EXPECT_TRUE(result.mPersonaName.empty());
    EXPECT_FALSE(result.mMaxColumns.has_value());
}

// ProfileFormTest_MakeNewProfileMatchesTheNewProfileDefault checks that new profiles use the specified host, port, login and persona defaults.
TEST(ProfileFormTest, MakeNewProfileMatchesTheNewProfileDefault)
{
    const persistence::Profile profile { makeNewProfile() };
    EXPECT_FALSE(profile.mId.has_value());
    EXPECT_EQ(profile.mHost, "mud2.co.uk");
    EXPECT_EQ(profile.mPort, 23);
    EXPECT_EQ(profile.mLoginName, "mud");
    EXPECT_TRUE(profile.mManagedPersonaSelection);
}
} // namespace
} // namespace muckaii::tui
