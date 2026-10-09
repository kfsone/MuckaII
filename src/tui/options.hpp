#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/persistence/profileStore.hpp"

#include "numbers.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace muckaii::tui
{
// OptPath is a file path that may be absent.
using OptPath = std::optional<std::filesystem::path>;

// Options contains parsed command-line values and whether startup selects direct connect.
struct Options
{
    types::OptString mProfileName; // --profile: select a saved profile
    std::string      mHost;
    std::string      mAccount;
    types::OptString mPassword;
    types::OptString mLoginName; // --user: override the telnet login name
    OptPath          mWireLog;
    types::OptInt    mMaxColumns;
    std::uint16_t    mPort { kDefaultPort };
    bool             mIsHelp {};

    // True when any of --profile --host --port --user --account --password was given
    // (connection.md 6 FACT): connect once at startup, never saved. False opens the Profile List.
    bool mIsDirectConnect {};
};

// parseArguments parses the program arguments; it returns nullopt and sets `error` for an unknown
// flag, missing value or non-printable credential. Without a direct-connect flag it selects the
// Profile List, so `--host` is not mandatory.
[[nodiscard]] std::optional<Options> parseArguments(std::span<char* const> args, std::string& error);

// findGivenPassword chooses `--password` before `environment`, returning nullopt when neither exists.
[[nodiscard]] types::OptString findGivenPassword(const Options& options, const types::OptString& environment);

// applyProfile fills unset command-line values from `profile`; explicit values win, and an empty
// profile password does not replace the current value.
void applyProfile(Options& options, const persistence::Profile& profile);
} // namespace muckaii::tui
