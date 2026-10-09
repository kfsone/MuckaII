#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/persistence/profileStore.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

// The Profile edit form's pure logic (docs/spec/flow.md "Profile fields"): converting a
// persistence::Profile to and from the text shown in each field, independent of FTXUI. Every
// function here is a pure translation; nothing touches the store or the terminal.
namespace muckaii::tui
{
// kLoginNameCount counts the supported shell accounts a profile can cycle through.
constexpr std::size_t kLoginNameCount { 4 };

// kLoginNames are the shell accounts a profile's Login field cycles through, in connection order.
constexpr std::array<std::string_view, kLoginNameCount> kLoginNames {
    "mud", "mudx", "mudguest", "mudguestx"
};

// kDefaultLoginName is the login a new profile and a direct connect use when none is chosen.
constexpr std::string_view kDefaultLoginName { kLoginNames[0] };

// cycleLoginName returns the entry of kLoginNames one step from `current` in `direction` (+1 or
// -1, any magnitude), wrapping at either end. A `current` that is not in the list counts as the
// first entry, so an old or hand-edited profile still cycles.
[[nodiscard]] std::string_view cycleLoginName(std::string_view current, int direction) noexcept;

// HostPort holds a split host and optional port; an absent or unparsable port lets the form keep
// the profile's existing port.
struct HostPort
{
    std::string   mHost;
    types::OptInt mPort;
};

// splitHostPort splits a trimmed "host[:port]" at its last colon, keeping an invalid suffix in the host.
[[nodiscard]] HostPort splitHostPort(std::string_view text);

// joinHostPort returns "host:port", or just "host" when `port` is kDefaultPort.
[[nodiscard]] std::string joinHostPort(std::string_view host, int port);

// formatMaxColumnsText returns the Terminal Width field text for `maxColumns`: blank when unset
// (the frontend default), otherwise the number, "0" meaning client-side wrap (connection.md 4.2).
[[nodiscard]] std::string formatMaxColumnsText(types::OptInt maxColumns);

// MaxColumnsParse holds the parsed Terminal Width value or its validation error.
struct MaxColumnsParse
{
    types::OptInt mMaxColumns;
    std::string   mError;

    // isValid reports whether parsing produced no validation error.
    [[nodiscard]] bool isValid() const noexcept
    {
        return mError.empty();
    }
};

// parseMaxColumnsText reads the Terminal Width field: blank is unset, "0" is client-side wrap, and
// any other value must be session::kMinColumns through session::kMaxColumns.
[[nodiscard]] MaxColumnsParse parseMaxColumnsText(std::string_view text);

// ProfileFields holds the edit form as text, including values not yet valid for a saved Profile.
struct ProfileFields
{
    std::string mName;
    std::string mHostPort;
    std::string mLoginName; // always one of kLoginNames; cycled, never freely typed
    std::string mAccount;
    std::string mPassword;
    std::string mPersonaName;
    std::string mMaxColumnsText;
};

// toFields returns the fields a saved profile shows when its edit form opens.
[[nodiscard]] ProfileFields toFields(const persistence::Profile& profile);

// ProfileResult holds the validated profile or the reason no profile could be produced.
struct ProfileResult
{
    persistence::OptProfile mProfile;
    std::string             mError;
};

// fromFields applies `fields` onto `base`, keeping its id, MRU timestamp and managed-persona flag.
// The result carries an error, and no profile, when the name or host is blank, the port is out of
// range, the account, password or persona is not printable 7-bit (it would end or corrupt the line
// it is sent in), or the Terminal Width text does not parse.
[[nodiscard]] ProfileResult fromFields(const ProfileFields& fields, const persistence::Profile& base);

// makeNewProfile returns an unsaved profile (mId empty) at connection.md 1's New-profile default:
// host mud2.co.uk, port kDefaultPort, Login persistence::kDefaultLoginName, managed persona selection on.
[[nodiscard]] persistence::Profile makeNewProfile();
} // namespace muckaii::tui
