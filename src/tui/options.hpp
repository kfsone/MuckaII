#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace muckaii::tui
{
constexpr std::uint16_t kDefaultPort { 23 };

// The command line (docs/spec/connection.md 6).
struct Options
{
    std::string                          mHost;
    std::string                          mAccount;
    std::optional<std::string>           mPassword;
    std::optional<std::filesystem::path> mWireLog;
    std::optional<int>                   mMaxColumns;
    std::uint16_t                        mPort { kDefaultPort };
    bool                                 mIsHelp {};
};

// Parses `args` (args[0] is the program). nullopt with `error` set on an unknown flag, a missing
// value, an account ID or password MUD2 cannot receive (not printable 7-bit), or no --host.
[[nodiscard]] std::optional<Options> parseArguments(std::span<char* const> args, std::string& error);

// The password to log in with, strongest first: --password, then the MUCKAII_PASSWORD value
// `environment`. nullopt when neither is given: the frontend asks at the terminal.
[[nodiscard]] std::optional<std::string> findGivenPassword(const Options&                    options,
                                                           const std::optional<std::string>& environment);
} // namespace muckaii::tui
