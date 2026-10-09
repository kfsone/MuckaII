#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/persistence/wireLog.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// WireReader reads a wire-log database that WireLog wrote, without ever modifying it. It is safe
// to open while a live client is still appending to the same file.
namespace muckaii::persistence
{
// RunSummary describes one recorded connection and how much wire traffic it holds.
struct RunSummary
{
    std::int64_t    mId {};
    std::int64_t    mStartedMs {};
    types::OptInt64 mEndedMs; // empty while the run is open or the client died
    std::string     mHost;
    std::string     mClientVersion;
    std::uint64_t   mRecords {};
};

// RunSummaries are the runs of one database, oldest first.
using RunSummaries = std::vector<RunSummary>;

// WireRecords are stored wire-log records, in sequence order.
using WireRecords = std::vector<WireRecord>;

// SeqRange selects the inclusive sequence numbers of a run; an absent bound is open.
struct SeqRange
{
    types::OptUint64 mFirst;
    types::OptUint64 mLast;
};

// WireReader is a read-only view of one wire-log database.
class WireReader
{
public:
    // WireReader opens `path` read-only; isOpen() and getError() report the outcome.
    explicit WireReader(const std::filesystem::path& path);

    // ~WireReader closes the database.
    ~WireReader();
    WireReader(const WireReader&)            = delete;
    WireReader& operator=(const WireReader&) = delete;
    WireReader(WireReader&&)                 = delete;
    WireReader& operator=(WireReader&&)      = delete;

    // isOpen reports whether the database opened and has a readable wire-log schema.
    [[nodiscard]] bool isOpen() const noexcept;

    // getError returns the latest failure message, or empty after a success.
    [[nodiscard]] const std::string& getError() const noexcept;

    // listRuns returns every run with its record count, oldest first.
    [[nodiscard]] RunSummaries listRuns();

    // readRecords returns the records of `run` inside `range`, in sequence order. A run that does
    // not exist, or a failed read, returns nothing and sets the error.
    [[nodiscard]] WireRecords readRecords(std::int64_t run, const SeqRange& range = {});

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
    std::string           mError;
};
} // namespace muckaii::persistence
