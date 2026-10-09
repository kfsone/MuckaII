#include "muckaii/persistence/wireReader.hpp"

#include <sqlite3.h>

#include <limits>
#include <memory>
#include <string_view>
#include <utility>

namespace muckaii::persistence
{
namespace
{
// kRunListSql selects each run with the number of records it holds.
constexpr char kRunListSql[] { "SELECT r.id, r.started_ms, r.ended_ms, r.host, r.client_version,"
                               " (SELECT COUNT(*) FROM wire AS w WHERE w.run = r.id)"
                               " FROM run AS r ORDER BY r.id" };
// kRunExistsSql selects the run row a read names.
constexpr char kRunExistsSql[] { "SELECT 1 FROM run WHERE id = ?1" };
// kRecordSql selects the records of one run between two sequence numbers, inclusive.
constexpr char kRecordSql[] {
    "SELECT seq, ts_ms, direction, data FROM wire_log WHERE run = ?1 AND seq >= ?2 AND seq <= ?3 ORDER BY seq"
};

// kColumn* name the columns of the run-list query.
constexpr int kColumnRunId { 0 };
constexpr int kColumnRunStarted { 1 };
constexpr int kColumnRunEnded { 2 };
constexpr int kColumnRunHost { 3 };
constexpr int kColumnRunVersion { 4 };
constexpr int kColumnRunRecords { 5 };

// kColumn* name the columns of the record query.
constexpr int kColumnSeq { 0 };
constexpr int kColumnTimestamp { 1 };
constexpr int kColumnDirection { 2 };
constexpr int kColumnData { 3 };

// kBind* number the parameters of the queries above.
constexpr int kBindRun { 1 };
constexpr int kBindFirst { 2 };
constexpr int kBindLast { 3 };

// ConnectionCloser releases an SQLite connection.
struct ConnectionCloser
{
    // operator() closes `connection`.
    void operator()(sqlite3* connection) const noexcept
    {
        sqlite3_close_v2(connection);
    }
};

// StatementFinalizer releases an SQLite statement.
struct StatementFinalizer
{
    // operator() finalizes `statement`.
    void operator()(sqlite3_stmt* statement) const noexcept
    {
        sqlite3_finalize(statement);
    }
};

using ConnectionPtr = std::unique_ptr<sqlite3, ConnectionCloser>;
using StatementPtr  = std::unique_ptr<sqlite3_stmt, StatementFinalizer>;

// getText returns column `column` of `statement` as a string, or empty for NULL.
std::string getText(sqlite3_stmt* statement, int column)
{
    const unsigned char* const text { sqlite3_column_text(statement, column) };
    if (text == nullptr)
        return {};
    return std::string { reinterpret_cast<const char*>(text),
                         static_cast<std::size_t>(sqlite3_column_bytes(statement, column)) };
}

// getBytes returns column `column` of `statement` as bytes.
types::Bytes getBytes(sqlite3_stmt* statement, int column)
{
    const void* const data { sqlite3_column_blob(statement, column) };
    const auto        size { static_cast<std::size_t>(sqlite3_column_bytes(statement, column)) };
    if (data == nullptr)
        return {};
    const auto* const first { static_cast<const std::byte*>(data) };
    return types::Bytes { first, first + size };
}

// isDirection reports whether `value` names a WireDirection.
bool isDirection(int value) noexcept
{
    return value >= static_cast<int>(WireDirection::kReceived)
           && value <= static_cast<int>(WireDirection::kAnnotation);
}
} // namespace

// Impl owns the read-only connection.
class WireReader::Impl
{
public:
    // open connects read-only to `path` and returns the failure text, or empty.
    std::string open(const std::filesystem::path& path)
    {
        sqlite3*  raw {};
        const int code { sqlite3_open_v2(toUtf8(path).c_str(), &raw, SQLITE_OPEN_READONLY, nullptr) };
        mConnection.reset(raw);
        if (code != SQLITE_OK)
            return std::string { "cannot open: " }
                   + (raw != nullptr ? sqlite3_errmsg(raw) : sqlite3_errstr(code));
        return {};
    }

    // prepare compiles `sql`; on failure it returns nothing and sets `error`.
    StatementPtr prepare(const char* sql, std::string& error)
    {
        sqlite3_stmt* raw {};
        if (sqlite3_prepare_v2(mConnection.get(), sql, -1, &raw, nullptr) != SQLITE_OK)
        {
            error = sqlite3_errmsg(mConnection.get());
            return nullptr;
        }
        return StatementPtr { raw };
    }

    // getMessage returns SQLite's latest message.
    [[nodiscard]] std::string getMessage() const
    {
        return sqlite3_errmsg(mConnection.get());
    }

private:
    ConnectionPtr mConnection;
};

WireReader::WireReader(const std::filesystem::path& path)
    : mImpl { std::make_unique<Impl>() }
{
    mError = mImpl->open(path);
    if (! mError.empty())
        return;
    // Opening a file that is not a wire log fails here, with a readable reason.
    std::string probeError;
    if (mImpl->prepare(kRunListSql, probeError) == nullptr)
        mError = "not a wire log: " + probeError;
}

WireReader::~WireReader() = default;

bool WireReader::isOpen() const noexcept
{
    return mError.empty();
}

const std::string& WireReader::getError() const noexcept
{
    return mError;
}

RunSummaries WireReader::listRuns()
{
    mError.clear();
    const StatementPtr statement { mImpl->prepare(kRunListSql, mError) };
    RunSummaries       runs;
    if (statement == nullptr)
        return runs;
    int code { sqlite3_step(statement.get()) };
    for (; code == SQLITE_ROW; code = sqlite3_step(statement.get()))
    {
        RunSummary run { .mId        = sqlite3_column_int64(statement.get(), kColumnRunId),
                         .mStartedMs = sqlite3_column_int64(statement.get(), kColumnRunStarted) };
        if (sqlite3_column_type(statement.get(), kColumnRunEnded) != SQLITE_NULL)
            run.mEndedMs = sqlite3_column_int64(statement.get(), kColumnRunEnded);
        run.mHost          = getText(statement.get(), kColumnRunHost);
        run.mClientVersion = getText(statement.get(), kColumnRunVersion);
        run.mRecords = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), kColumnRunRecords));
        runs.push_back(std::move(run));
    }
    if (code != SQLITE_DONE)
        mError = mImpl->getMessage();
    return runs;
}

WireRecords WireReader::readRecords(std::int64_t run, const SeqRange& range)
{
    mError.clear();
    constexpr auto kMaxBound { static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) };
    if (range.mFirst.value_or(0) > kMaxBound || range.mLast.value_or(0) > kMaxBound)
    {
        mError = "sequence bound out of range";
        return {};
    }
    const StatementPtr exists { mImpl->prepare(kRunExistsSql, mError) };
    if (exists == nullptr)
        return {};
    sqlite3_bind_int64(exists.get(), kBindRun, run);
    if (sqlite3_step(exists.get()) != SQLITE_ROW)
    {
        mError = "no run " + std::to_string(run);
        return {};
    }

    const StatementPtr statement { mImpl->prepare(kRecordSql, mError) };
    if (statement == nullptr)
        return {};
    sqlite3_bind_int64(statement.get(), kBindRun, run);
    sqlite3_bind_int64(statement.get(), kBindFirst, static_cast<std::int64_t>(range.mFirst.value_or(0)));
    sqlite3_bind_int64(statement.get(),
                       kBindLast,
                       range.mLast.has_value() ? static_cast<std::int64_t>(*range.mLast)
                                               : std::numeric_limits<std::int64_t>::max());
    WireRecords records;
    int         code { sqlite3_step(statement.get()) };
    for (; code == SQLITE_ROW; code = sqlite3_step(statement.get()))
    {
        const int direction { sqlite3_column_int(statement.get(), kColumnDirection) };
        if (! isDirection(direction))
        {
            mError = "bad direction " + std::to_string(direction) + " at seq "
                     + std::to_string(sqlite3_column_int64(statement.get(), kColumnSeq));
            return {};
        }
        records.push_back(WireRecord {
            .mSequence    = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), kColumnSeq)),
            .mTimestampMs = sqlite3_column_int64(statement.get(), kColumnTimestamp),
            .mData        = getBytes(statement.get(), kColumnData),
            .mDirection   = static_cast<WireDirection>(direction) });
    }
    if (code != SQLITE_DONE)
    {
        mError = mImpl->getMessage();
        return {};
    }
    return records;
}
} // namespace muckaii::persistence
