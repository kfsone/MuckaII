#include "options.hpp"

#include "muckaii/session/session.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <string_view>

namespace muckaii::tui
{
namespace
{
// What one command-line word is (connection.md 6): not a flag, a flag MuckaII does not know, or
// one of these.
enum class Flag : std::uint8_t
{
    kNotAFlag,
    kUnknown,
    kHost,
    kPort,
    kAccount,
    kPassword,
    kMaxColumns,
    kWireLog,
    kHelp,
};

std::string toLower(std::string_view text)
{
    std::string lower;
    for (const char c : text)
        lower.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    return lower;
}

std::optional<int> parseInt(std::string_view text) noexcept
{
    int        value {};
    const auto parsed { std::from_chars(text.data(), text.data() + text.size(), value) };
    if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

// A flag is -name with any number of leading dashes, case-insensitive, followed by its value
// (connection.md 6). Words that are not flags are ignored.
Flag getFlag(std::string_view arg)
{
    struct Name
    {
        std::string_view mText;
        Flag             mFlag {};
    };
    static constexpr std::array<Name, 7> kNames { {
        { .mText = "host", .mFlag = Flag::kHost },
        { .mText = "port", .mFlag = Flag::kPort },
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
    for (std::size_t i { 1 }; i < args.size(); ++i)
    {
        const std::string_view arg { args[i] };
        const Flag             flag { getFlag(arg) };
        if (flag == Flag::kNotAFlag)
            continue;
        if (flag == Flag::kUnknown)
        {
            error = "unknown flag " + std::string { arg };
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
            error = "missing value for " + std::string { arg };
            return std::nullopt;
        }
        const std::string_view value { args[++i] };
        switch (flag)
        {
            case Flag::kHost:
                options.mHost = value;
                break;
            case Flag::kAccount:
            case Flag::kPassword:
                // A credential with a control or high byte would end or corrupt its line.
                if (! session::isValidCredential(value))
                {
                    error = std::string { arg } + " takes printable 7-bit characters only";
                    return std::nullopt;
                }
                if (flag == Flag::kAccount)
                    options.mAccount = value;
                else
                    options.mPassword = std::string { value };
                break;
            case Flag::kWireLog:
                options.mWireLog = std::filesystem::path { value };
                break;
            case Flag::kMaxColumns:
            {
                const std::optional<int> columns { parseInt(value) };
                if (! columns.has_value() || *columns < session::kMinColumns
                    || *columns > session::kMaxColumns)
                {
                    error = "--max-columns takes a number from 20 to 160";
                    return std::nullopt;
                }
                options.mMaxColumns = columns;
                break;
            }
            case Flag::kPort:
            {
                // Out of range or not a number: ignored without warning (connection.md 6).
                const std::optional<int> port { parseInt(value) };
                constexpr int            kMaxPort { 65535 };
                if (port.has_value() && *port >= 1 && *port <= kMaxPort)
                    options.mPort = static_cast<std::uint16_t>(*port);
                break;
            }
            case Flag::kNotAFlag:
            case Flag::kUnknown:
            case Flag::kHelp:
                break;
        }
    }
    if (! options.mIsHelp && options.mHost.empty())
    {
        error = "--host is required";
        return std::nullopt;
    }
    return options;
}

std::optional<std::string> findGivenPassword(const Options&                    options,
                                             const std::optional<std::string>& environment)
{
    if (options.mPassword.has_value())
        return options.mPassword;
    return environment;
}
} // namespace muckaii::tui
