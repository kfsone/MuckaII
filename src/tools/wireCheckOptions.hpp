#pragma once

#include "muckaii/base/types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace muckaii::tools
{
// OptPath is a file path that may be absent.
using OptPath = std::optional<std::filesystem::path>;

// WireCheckAction is what the tool was asked to do.
enum class WireCheckAction : std::uint8_t
{
    kHelp,
    kList,   // list the runs
    kCheck,  // decode runs and report findings
    kExport, // write part of a run as a capture file
};

// WireCheckOptions holds the parsed command line.
struct WireCheckOptions
{
    WireCheckAction       mAction { WireCheckAction::kCheck };
    OptPath               mDatabase; // empty selects the standard wire log
    types::OptInt64       mRun;      // empty selects every run (check) and is required to export
    types::OptUint64      mFirstSeq;
    types::OptUint64      mLastSeq;
    std::filesystem::path mExportPath;
    bool                  mIsIncludingSent {};
};

// parseWireCheckArguments parses `args` (without the program name). It returns nothing and sets
// `error` for an unknown flag, a missing or malformed value, or an export that lacks its run.
[[nodiscard]] std::optional<WireCheckOptions> parseWireCheckArguments(std::span<const std::string> args,
                                                                      std::string&                 error);

// getWireCheckUsage returns the help text.
[[nodiscard]] const char* getWireCheckUsage() noexcept;
} // namespace muckaii::tools
