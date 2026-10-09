#include "profileForm.hpp"

#include "muckaii/session/session.hpp"

#include "numbers.hpp"

#include <algorithm>
#include <array>

namespace muckaii::tui
{
namespace
{
// kBlankCharacters are the characters trimmed from editable fields.
constexpr std::string_view kBlankCharacters { " \t" };

// kColumnsError explains the accepted Terminal Width field values.
constexpr std::string_view kColumnsError {
    "Terminal Width takes a number from 20 to 160, 0 for client-side wrap, or blank"
};
// kCredentialErrorSuffix explains the allowed characters in credentials.
constexpr std::string_view kCredentialErrorSuffix { " takes printable 7-bit ascii characters only" };

// kPortError describes the accepted Host field port suffix.
constexpr std::string_view kPortError { "Host takes an optional :port from 1 to 65535" };

// kDefaultHost is the endpoint assigned to a newly created profile.
constexpr std::string_view kDefaultHost { "mud2.co.uk" };

// kNewProfileName is the initial name shown for a new unsaved profile.
constexpr std::string_view kNewProfileName { "New profile" };

// kBlankNameError rejects profiles without a display name.
constexpr std::string_view kBlankNameError { "Name cannot be blank" };

// kBlankHostError rejects profiles without an endpoint.
constexpr std::string_view kBlankHostError { "Host cannot be blank" };

// CredentialField names one server-bound text field and its validation label.
struct CredentialField
{
    std::string_view mText;
    std::string_view mLabel;
};

// trim returns `text` without leading or trailing spaces and tabs.
std::string_view trim(std::string_view text) noexcept
{
    const auto first { text.find_first_not_of(kBlankCharacters) };
    if (first == std::string_view::npos)
        return {};
    const auto last { text.find_last_not_of(kBlankCharacters) };
    return text.substr(first, last - first + 1);
}

// isValidColumns reports whether `columns` is client-side wrap or within the allowed ceiling.
bool isValidColumns(int columns) noexcept
{
    const bool isWrap { columns == 0 };
    const bool isCeiling { columns >= session::kMinColumns && columns <= session::kMaxColumns };
    return isWrap || isCeiling;
}

// findRejectedCredentialLabel returns the label of the first of the account, password and persona
// fields that is not printable 7-bit text, or an empty view when all of them are acceptable.
std::string_view findRejectedCredentialLabel(const ProfileFields& fields) noexcept
{
    constexpr std::size_t                                    kCredentialFieldCount { 3 };
    const std::array<CredentialField, kCredentialFieldCount> credentials { {
        { .mText = fields.mAccount, .mLabel = "Account" },
        { .mText = fields.mPassword, .mLabel = "Password" },
        { .mText = fields.mPersonaName, .mLabel = "Persona" },
    } };
    for (const CredentialField& credential : credentials)
    {
        if (! credential.mText.empty() && ! session::isValidCredential(credential.mText))
            return credential.mLabel;
    }
    return {};
}

// makeFailure returns a result with no profile and the supplied validation error.
ProfileResult makeFailure(std::string error)
{
    return ProfileResult { .mProfile = std::nullopt, .mError = std::move(error) };
}
} // namespace

std::string_view cycleLoginName(std::string_view current, int direction) noexcept
{
    const auto  found { std::ranges::find(kLoginNames, current) };
    std::size_t index {};
    if (found != kLoginNames.end())
        index = static_cast<std::size_t>(found - kLoginNames.begin());
    const int count { static_cast<int>(kLoginNames.size()) };
    // `direction` may be any sign; wrap into [0, count) without relying on the sign of `%`.
    const int step { ((direction % count) + count) % count };
    return kLoginNames[(index + static_cast<std::size_t>(step)) % kLoginNames.size()];
}

HostPort splitHostPort(std::string_view text)
{
    text = trim(text);
    const auto colon { text.find_last_of(':') };
    if (colon == std::string_view::npos)
        return HostPort { .mHost = std::string { text }, .mPort = std::nullopt };
    const std::string_view hostPart { text.substr(0, colon) };
    const types::OptInt    port { parseInt(text.substr(colon + 1)) };
    if (! port.has_value())
        return HostPort { .mHost = std::string { text }, .mPort = std::nullopt };
    // An empty host before a valid port (":23") is a deliberately host-less value: fromFields
    // reports it as a blank host rather than misreading it as a hostname.
    return HostPort { .mHost = std::string { hostPart }, .mPort = port };
}

std::string joinHostPort(std::string_view host, int port)
{
    if (std::cmp_equal(port, kDefaultPort))
        return std::string { host };
    return std::string { host } + ":" + std::to_string(port);
}

std::string formatMaxColumnsText(types::OptInt maxColumns)
{
    if (! maxColumns.has_value())
        return {};
    return std::to_string(*maxColumns);
}

MaxColumnsParse parseMaxColumnsText(std::string_view text)
{
    text = trim(text);
    if (text.empty())
        return MaxColumnsParse {};
    const types::OptInt value { parseInt(text) };
    if (! value.has_value() || ! isValidColumns(*value))
        return MaxColumnsParse { .mMaxColumns = std::nullopt, .mError = std::string { kColumnsError } };
    return MaxColumnsParse { .mMaxColumns = value, .mError = {} };
}

ProfileFields toFields(const persistence::Profile& profile)
{
    return ProfileFields {
        .mName           = profile.mName,
        .mHostPort       = joinHostPort(profile.mHost, profile.mPort),
        .mLoginName      = profile.mLoginName,
        .mAccount        = profile.mAccount,
        .mPassword       = profile.mPassword,
        .mPersonaName    = profile.mPersonaName,
        .mMaxColumnsText = formatMaxColumnsText(profile.mMaxColumns),
    };
}

ProfileResult fromFields(const ProfileFields& fields, const persistence::Profile& base)
{
    const std::string_view name { trim(fields.mName) };
    if (name.empty())
        return makeFailure(std::string { kBlankNameError });

    const HostPort hostPort { splitHostPort(fields.mHostPort) };
    if (hostPort.mHost.empty())
        return makeFailure(std::string { kBlankHostError });

    const int port { hostPort.mPort.value_or(base.mPort) };
    if (! isValidPort(port))
        return makeFailure(std::string { kPortError });

    const std::string_view rejected { findRejectedCredentialLabel(fields) };
    if (! rejected.empty())
        return makeFailure(std::string { rejected } + std::string { kCredentialErrorSuffix });

    const MaxColumnsParse columns { parseMaxColumnsText(fields.mMaxColumnsText) };
    if (! columns.isValid())
        return makeFailure(columns.mError);

    persistence::Profile result { base };
    result.mName        = std::string { name };
    result.mHost        = hostPort.mHost;
    result.mPort        = port;
    result.mLoginName   = fields.mLoginName;
    result.mAccount     = fields.mAccount;
    result.mPassword    = fields.mPassword;
    result.mPersonaName = fields.mPersonaName;
    result.mMaxColumns  = columns.mMaxColumns;
    return ProfileResult { .mProfile = std::move(result), .mError = {} };
}

persistence::Profile makeNewProfile()
{
    persistence::Profile profile;
    profile.mHost      = std::string { kDefaultHost };
    profile.mPort      = kDefaultPort;
    profile.mLoginName = persistence::kDefaultLoginName;
    profile.mName      = std::string { kNewProfileName };
    return profile;
}
} // namespace muckaii::tui
