#include "options.hpp"

#include "muckaii/base/types.hpp"
#include "muckaii/session/session.hpp"

#include "numbers.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace muckaii::tui
{
namespace
{
// kFirstOptionArgument is the first argv entry after the program name.
constexpr std::size_t kFirstOptionArgument { 1 };

// kFlagNameCount is the number of recognized long-option spellings.
constexpr std::size_t kFlagNameCount { 9 };

// kUnknownFlagPrefix begins the diagnostic for an unsupported option.
constexpr std::string_view kUnknownFlagPrefix { "unknown flag " };

// kMissingValuePrefix begins the diagnostic for an option without a value.
constexpr std::string_view kMissingValuePrefix { "missing value for " };

// kCredentialErrorSuffix explains the required command-line credential characters.
constexpr std::string_view kCredentialErrorSuffix { " takes printable 7-bit characters only" };

// kMaxColumnsError describes the accepted server column range.
constexpr std::string_view kMaxColumnsError { "--max-columns takes a number from 20 to 160" };

// Flag classifies a command-line word before its value is parsed.
enum class Flag : std::uint8_t
{
    kNotAFlag,
    kUnknown,
    kProfile,
    kHost,
    kPort,
    kUser,
    kAccount,
    kPassword,
    kMaxColumns,
    kWireLog,
    kHelp,
};

// toLower folds ASCII letters in a command-line flag to lowercase.
std::string toLower(std::string_view text)
{
    std::string lower;
    for (const char c : text)
        lower.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    return lower;
}

// getFlag recognizes known dash-prefixed options without regard to case.
Flag getFlag(std::string_view arg)
{
    // Name associates one option spelling with its parsed flag.
    struct Name
    {
        std::string_view mText;
        Flag             mFlag {};
    };
    // kNames maps each recognized command-line spelling to its flag.
    static constexpr std::array<Name, kFlagNameCount> kNames { {
        { .mText = "profile", .mFlag = Flag::kProfile },
        { .mText = "host", .mFlag = Flag::kHost },
        { .mText = "port", .mFlag = Flag::kPort },
        { .mText = "user", .mFlag = Flag::kUser },
        { .mText = "account", .mFlag = Flag::kAccount },
        { .mText = "password", .mFlag = Flag::kPassword },
        { .mText = "max-columns", .mFlag = Flag::kMaxColumns },
        { .mText = "wire-log", .mFlag = Flag::kWireLog },
        { .mText = "help", .mFlag = Flag::kHelp },
    } };
    if (! arg.starts_with('-'))
        return Flag::kNotAFlag;
    while (arg.starts_with('-'))
        arg.remove_prefix(1);
    const std::string name { toLower(arg) };
    const auto        found { std::ranges::find(kNames, name, &Name::mText) };
    return found != kNames.end() ? found->mFlag : Flag::kUnknown;
}
} // namespace

std::optional<Options> parseArguments(std::span<char* const> args, std::string& error)
{
    Options options;
    for (std::size_t i { kFirstOptionArgument }; i < args.size(); ++i)
    {
        const std::string_view arg { args[i] };
        const Flag             flag { getFlag(arg) };
        if (flag == Flag::kNotAFlag)
            continue;
        if (flag == Flag::kUnknown)
        {
            error = std::string { kUnknownFlagPrefix } + std::string { arg };
            return std::nullopt;
        }
        if (flag == Flag::kHelp)
        {
            options.mIsHelp = true;
            continue;
        }
        // A value that is itself a known flag is a missing value (connection.md 6); "-x" that is
        // not one is a value, so a password may start with '-'.
        const Flag next { i + 1 < args.size() ? getFlag(args[i + 1]) : Flag::kNotAFlag };
        if (i + 1 >= args.size() || (next != Flag::kNotAFlag && next != Flag::kUnknown))
        {
            error = std::string { kMissingValuePrefix } + std::string { arg };
            return std::nullopt;
        }
        const std::string_view value { args[++i] };
        if (flag == Flag::kProfile || flag == Flag::kHost || flag == Flag::kPort || flag == Flag::kUser
            || flag == Flag::kAccount || flag == Flag::kPassword)
            options.mIsDirectConnect = true; // connection.md 6 FACT
        switch (flag)
        {
            case Flag::kProfile:
                options.mProfileName = std::string { value };
                break;
            case Flag::kHost:
                options.mHost = value;
                break;
            case Flag::kUser:
            case Flag::kAccount:
            case Flag::kPassword:
                // A credential with a control or high byte would end or corrupt its line.
                if (! session::isValidCredential(value))
                {
                    error = std::string { arg } + std::string { kCredentialErrorSuffix };
                    return std::nullopt;
                }
                if (flag == Flag::kUser)
                    options.mLoginName = std::string { value };
                else if (flag == Flag::kAccount)
                    options.mAccount = value;
                else
                    options.mPassword = std::string { value };
                break;
            case Flag::kWireLog:
                options.mWireLog = std::filesystem::path { value };
                break;
            case Flag::kMaxColumns:
            {
                const types::OptInt columns { parseInt(value) };
                if (! columns.has_value() || *columns < session::kMinColumns
                    || *columns > session::kMaxColumns)
                {
                    error = std::string { kMaxColumnsError };
                    return std::nullopt;
                }
                options.mMaxColumns = columns;
                break;
            }
            case Flag::kPort:
            {
                // Out of range or not a number: ignored without warning (connection.md 6).
                const types::OptInt port { parseInt(value) };
                if (port.has_value() && isValidPort(*port))
                    options.mPort = static_cast<std::uint16_t>(*port);
                break;
            }
            case Flag::kNotAFlag:
            case Flag::kUnknown:
            case Flag::kHelp:
                break;
        }
    }
    return options;
}

types::OptString findGivenPassword(const Options& options, const types::OptString& environment)
{
    if (options.mPassword.has_value())
        return options.mPassword;
    return environment;
}
void applyProfile(Options& options, const persistence::Profile& profile)
{
    if (options.mHost.empty())
    {
        options.mHost = profile.mHost;
        options.mPort = static_cast<std::uint16_t>(profile.mPort);
    }
    if (options.mAccount.empty())
        options.mAccount = profile.mAccount;
    if (! options.mPassword.has_value() && ! profile.mPassword.empty())
        options.mPassword = profile.mPassword;
    if (! options.mMaxColumns.has_value())
        options.mMaxColumns = profile.mMaxColumns;
    if (! options.mLoginName.has_value())
        options.mLoginName = profile.mLoginName;
}
} // namespace muckaii::tui
