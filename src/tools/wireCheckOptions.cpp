#include "wireCheckOptions.hpp"

#include <charconv>
#include <string_view>

namespace muckaii::tools
{
namespace
{
constexpr std::string_view kFlagHelp { "--help" };
constexpr std::string_view kFlagList { "--list" };
constexpr std::string_view kFlagCheck { "--check" };
constexpr std::string_view kFlagDb { "--db" };
constexpr std::string_view kFlagRun { "--run" };
constexpr std::string_view kFlagFrom { "--from" };
constexpr std::string_view kFlagTo { "--to" };
constexpr std::string_view kFlagExport { "--export" };
constexpr std::string_view kFlagWithSent { "--with-sent" };

// kUsage is the help text; it says what each action reads and writes.
constexpr char kUsage[] {
    "usage: muckaiiWireCheck [--db FILE] [--list | --check [--run N] | --export FILE --run N [--from SEQ] "
    "[--to SEQ] [--with-sent]]\n"
    "\n"
    "  --list         list the recorded runs\n"
    "  --check        decode runs and report findings (default; exit 1 if any run needs a look)\n"
    "  --export FILE  write records of run N (SEQ range inclusive) as a capture file; never overwrites\n"
    "  --with-sent    include tx records in the export; they may hold credentials, so review before sharing\n"
    "\n"
    "The default database is %USERPROFILE%\\.muckaii\\muckaii.db; it is opened read-only.\n"
};

// parseNumber parses all of `text` as a non-negative decimal into `out`.
template <typename Number> bool parseNumber(std::string_view text, Number& out)
{
    if (text.empty() || text.front() == '-')
        return false;
    const auto result { std::from_chars(text.data(), text.data() + text.size(), out) };
    return result.ec == std::errc {} && result.ptr == text.data() + text.size();
}

// takeValue returns the value after the flag at `index`, advancing it, or sets `error`.
std::optional<std::string_view>
takeValue(std::span<const std::string> args, std::size_t& index, std::string& error)
{
    if (index + 1 >= args.size())
    {
        error = args[index] + " needs a value";
        return std::nullopt;
    }
    return std::string_view { args[++index] };
}

// applyNumber parses the value of a numeric flag into `out`; it returns false and sets `error` when malformed.
template <typename Number>
bool applyNumber(std::span<const std::string> args,
                 std::size_t&                 index,
                 std::optional<Number>&       out,
                 std::string&                 error)
{
    const std::string& flag { args[index] };
    const auto         value { takeValue(args, index, error) };
    if (! value.has_value())
        return false;
    Number parsed {};
    if (! parseNumber(*value, parsed))
    {
        error = flag + " needs a non-negative whole number, not '" + std::string { *value } + "'";
        return false;
    }
    out = parsed;
    return true;
}

// applyFlag applies the flag at `index`; it returns false and sets `error` when it cannot.
bool applyFlag(std::span<const std::string> args,
               std::size_t&                 index,
               WireCheckOptions&            options,
               std::string&                 error)
{
    const std::string_view flag { args[index] };
    if (flag == kFlagHelp)
        options.mAction = WireCheckAction::kHelp;
    else if (flag == kFlagList)
        options.mAction = WireCheckAction::kList;
    else if (flag == kFlagCheck)
        options.mAction = WireCheckAction::kCheck;
    else if (flag == kFlagWithSent)
        options.mIsIncludingSent = true;
    else if (flag == kFlagRun)
        return applyNumber(args, index, options.mRun, error);
    else if (flag == kFlagFrom)
        return applyNumber(args, index, options.mFirstSeq, error);
    else if (flag == kFlagTo)
        return applyNumber(args, index, options.mLastSeq, error);
    else if (flag == kFlagDb || flag == kFlagExport)
    {
        const auto value { takeValue(args, index, error) };
        if (! value.has_value())
            return false;
        if (flag == kFlagDb)
            options.mDatabase = std::filesystem::path { *value };
        else
        {
            options.mAction     = WireCheckAction::kExport;
            options.mExportPath = std::filesystem::path { *value };
        }
    }
    else
    {
        error = "unknown argument '" + std::string { flag } + "'";
        return false;
    }
    return true;
}
} // namespace

std::optional<WireCheckOptions> parseWireCheckArguments(std::span<const std::string> args, std::string& error)
{
    WireCheckOptions options;
    for (std::size_t i {}; i < args.size(); ++i)
    {
        if (! applyFlag(args, i, options, error))
            return std::nullopt;
    }
    if (options.mAction == WireCheckAction::kExport && ! options.mRun.has_value())
    {
        error = "--export needs --run";
        return std::nullopt;
    }
    if (options.mFirstSeq.has_value() && options.mLastSeq.has_value()
        && *options.mFirstSeq > *options.mLastSeq)
    {
        error = "--from is after --to";
        return std::nullopt;
    }
    return options;
}

const char* getWireCheckUsage() noexcept
{
    return kUsage;
}
} // namespace muckaii::tools
