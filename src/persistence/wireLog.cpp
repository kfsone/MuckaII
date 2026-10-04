#include "muckaii/persistence/wireLog.hpp"

#include "muckaii/persistence/sha256.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace muckaii::persistence
{
namespace
{
// SQLite's own wait on a lock is kept short; the writer's backoff below waits longer, and stays
// interruptible by shutdown.
constexpr int                       kBusyTimeoutMs { 250 };
constexpr std::chrono::milliseconds kFirstBackOff { 50 };
constexpr std::chrono::milliseconds kMaxBackOff { 2000 };
constexpr std::chrono::milliseconds kCloseWait { 3000 };
// Jobs stored per transaction, which bounds the writer's work after shutdown is asked for.
constexpr std::size_t kMaxBatch { 512 };
constexpr int         kPrimaryCodeMask { 0xFF };

// Each step is forward-only and runs once; a released step is never edited (persistence.md).
constexpr std::array<std::string_view, kSchemaSteps> kSteps {
    "CREATE TABLE run ("
    " id INTEGER PRIMARY KEY,"
    " started_ms INTEGER NOT NULL,"
    " ended_ms INTEGER,"
    " host TEXT NOT NULL,"
    " client_version TEXT NOT NULL);"
    "CREATE TABLE payload ("
    " hash BLOB PRIMARY KEY,"
    " data BLOB NOT NULL) WITHOUT ROWID;"
    "CREATE TABLE wire ("
    " run INTEGER NOT NULL REFERENCES run(id),"
    " seq INTEGER NOT NULL,"
    " ts_ms INTEGER NOT NULL,"
    " direction INTEGER NOT NULL,"
    " payload BLOB NOT NULL REFERENCES payload(hash),"
    " PRIMARY KEY (run, seq)) WITHOUT ROWID;"
    "CREATE VIEW wire_log AS"
    " SELECT w.run AS run, w.seq AS seq, w.ts_ms AS ts_ms, w.direction AS direction, p.data AS data"
    " FROM wire AS w JOIN payload AS p ON p.hash = w.payload;",
};

std::int64_t getNowMs() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// An SQLite result: its code and, on failure, its message.
struct Status
{
    int         mCode { SQLITE_OK };
    std::string mMessage;

    [[nodiscard]] bool isOk() const noexcept
    {
        return mCode == SQLITE_OK || mCode == SQLITE_DONE;
    }

    // Another connection holds the lock: transient, so the writer retries (persistence.md).
    [[nodiscard]] bool isBusy() const noexcept
    {
        const int primary { mCode & kPrimaryCodeMask };
        return primary == SQLITE_BUSY || primary == SQLITE_LOCKED;
    }
};

struct ConnectionCloser
{
    void operator()(sqlite3* connection) const noexcept
    {
        sqlite3_close_v2(connection);
    }
};

struct StatementFinalizer
{
    void operator()(sqlite3_stmt* statement) const noexcept
    {
        sqlite3_finalize(statement);
    }
};

using ConnectionPtr = std::unique_ptr<sqlite3, ConnectionCloser>;
using StatementPtr  = std::unique_ptr<sqlite3_stmt, StatementFinalizer>;

std::chrono::milliseconds getNextBackOff(std::chrono::milliseconds delay) noexcept
{
    return std::min(delay * 2, kMaxBackOff);
}
} // namespace

std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string text { path.u8string() };
    std::string         out;
    out.reserve(text.size());
    for (const char8_t c : text)
        out.push_back(static_cast<char>(c));
    return out;
}

std::optional<std::filesystem::path> findDefaultDatabasePath()
{
#ifdef _WIN32
    const DWORD size { GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0) };
    if (size <= 1)
        return std::nullopt;
    std::wstring home(size, L'\0');
    const DWORD  written { GetEnvironmentVariableW(L"USERPROFILE", home.data(), size) };
    if (written == 0 || written >= size)
        return std::nullopt;
    home.resize(written);
#else
    const char* const value { std::getenv("HOME") };
    if (value == nullptr || *value == '\0')
        return std::nullopt;
    const std::string home { value };
#endif
    return std::filesystem::path { home } / ".muckaii" / std::string { kDatabaseName };
}

// The writer thread's database: opened, migrated and written only on that thread.
class WireLog::Database
{
public:
    // Opens and migrates.
    [[nodiscard]] Status open(const std::filesystem::path& path, std::vector<std::string>& notices)
    {
        close();
        std::error_code directoryError;
        std::filesystem::create_directories(path.parent_path(), directoryError);
        sqlite3*          raw {};
        const std::string name { toUtf8(path) };
        const int         code { sqlite3_open_v2(
            name.c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) };
        mConnection.reset(raw);
        if (code != SQLITE_OK)
            return Status { .mCode    = code,
                            .mMessage = raw != nullptr ? sqlite3_errmsg(raw) : "out of memory" };
        sqlite3_busy_timeout(raw, kBusyTimeoutMs);
        if (Status status { execute("PRAGMA foreign_keys=ON;") }; ! status.isOk())
            return status;
        // The pragma is silently a no-op inside a transaction or in a build without foreign keys:
        // read it back.
        if (readInteger("PRAGMA foreign_keys;") != 1)
            return Status { .mCode = SQLITE_ERROR, .mMessage = "foreign keys are not enforced" };
        if (Status status { execute("PRAGMA journal_mode=WAL;") }; ! status.isOk())
            return status;
        if (Status status { migrate(notices) }; ! status.isOk())
            return status;
        if (! prepare(mInsertRun, "INSERT INTO run (started_ms, host, client_version) VALUES (?, ?, ?);")
            || ! prepare(mEndRun, "UPDATE run SET ended_ms = ? WHERE id = ?;")
            || ! prepare(mInsertPayload, "INSERT OR IGNORE INTO payload (hash, data) VALUES (?, ?);")
            || ! prepare(mInsertWire,
                         "INSERT INTO wire (run, seq, ts_ms, direction, payload) VALUES (?, ?, ?, ?, ?);"))
            return Status { .mCode = sqlite3_errcode(raw), .mMessage = sqlite3_errmsg(raw) };
        return {};
    }

    // Stores a batch in one transaction. On failure nothing of it is kept, so it can be retried.
    [[nodiscard]] Status store(const std::vector<Job>& jobs)
    {
        const std::optional<std::int64_t> runBefore { mRunId };
        if (Status status { execute("BEGIN IMMEDIATE;") }; ! status.isOk())
            return status;
        for (const Job& job : jobs)
        {
            Status status;
            if (const auto* begin { std::get_if<BeginJob>(&job) }; begin != nullptr)
                status = storeBegin(begin->mInfo);
            else if (const auto* record { std::get_if<WireRecord>(&job) }; record != nullptr)
                status = storeRecord(*record);
            else if (const auto* end { std::get_if<EndJob>(&job) }; end != nullptr)
                status = storeEnd(end->mEndedMs);
            if (! status.isOk())
                return rollBack(std::move(status), runBefore);
        }
        if (Status status { execute("COMMIT;") }; ! status.isOk())
            return rollBack(std::move(status), runBefore);
        return {};
    }

private:
    void close() noexcept
    {
        mInsertRun.reset();
        mEndRun.reset();
        mInsertPayload.reset();
        mInsertWire.reset();
        mConnection.reset();
    }

    [[nodiscard]] Status rollBack(Status status, std::optional<std::int64_t> runBefore)
    {
        static_cast<void>(execute("ROLLBACK;"));
        mRunId = runBefore;
        return status;
    }

    [[nodiscard]] Status execute(const char* sql) const
    {
        char*     message {};
        const int code { sqlite3_exec(mConnection.get(), sql, nullptr, nullptr, &message) };
        if (code == SQLITE_OK)
            return {};
        Status status { .mCode = code, .mMessage = message != nullptr ? message : "unknown error" };
        sqlite3_free(message);
        return status;
    }

    [[nodiscard]] bool prepare(StatementPtr& statement, const char* sql) const
    {
        sqlite3_stmt* raw {};
        const bool isPrepared { sqlite3_prepare_v2(mConnection.get(), sql, -1, &raw, nullptr) == SQLITE_OK };
        statement.reset(raw);
        return isPrepared;
    }

    [[nodiscard]] Status step(const StatementPtr& statement) const
    {
        const int code { sqlite3_step(statement.get()) };
        sqlite3_reset(statement.get());
        sqlite3_clear_bindings(statement.get());
        if (code != SQLITE_DONE)
            return Status { .mCode = code, .mMessage = sqlite3_errmsg(mConnection.get()) };
        return {};
    }

    [[nodiscard]] Status migrate(std::vector<std::string>& notices) const
    {
        if (Status status { execute("CREATE TABLE IF NOT EXISTS schema_step (step INTEGER PRIMARY KEY, "
                                    "applied_ms INTEGER NOT NULL);") };
            ! status.isOk())
            return status;

        for (int step { 1 }; step <= kSchemaSteps; ++step)
        {
            if (Status status { execute("BEGIN IMMEDIATE;") }; ! status.isOk())
                return status;
            // Re-read inside the transaction: another instance may have run the step first.
            const std::set<int> applied { readSteps() };
            if (applied.contains(step))
            {
                static_cast<void>(execute("COMMIT;"));
                continue;
            }
            const std::string sql { std::string { kSteps[static_cast<std::size_t>(step - 1)] }
                                    + "INSERT INTO schema_step (step, applied_ms) VALUES ("
                                    + std::to_string(step) + ", " + std::to_string(getNowMs()) + ");" };
            if (Status status { execute(sql.c_str()) }; ! status.isOk())
            {
                static_cast<void>(execute("ROLLBACK;"));
                return status;
            }
            if (Status status { execute("COMMIT;") }; ! status.isOk())
            {
                static_cast<void>(execute("ROLLBACK;"));
                return status;
            }
        }

        const std::set<int> applied { readSteps() };
        if (! applied.empty() && *applied.rbegin() > kSchemaSteps)
        {
            notices.emplace_back("wire log: the database was written by a newer MuckaII; logging carries on "
                                 "with the tables this version knows");
        }
        return {};
    }

    // The first column of the first row; nullopt when there is none.
    [[nodiscard]] std::optional<int> readInteger(const char* sql) const
    {
        StatementPtr query;
        if (! prepare(query, sql) || sqlite3_step(query.get()) != SQLITE_ROW)
            return std::nullopt;
        return sqlite3_column_int(query.get(), 0);
    }

    [[nodiscard]] std::set<int> readSteps() const
    {
        std::set<int> steps;
        StatementPtr  query;
        if (! prepare(query, "SELECT step FROM schema_step;"))
            return steps;
        while (sqlite3_step(query.get()) == SQLITE_ROW)
            steps.insert(sqlite3_column_int(query.get(), 0));
        return steps;
    }

    [[nodiscard]] Status storeBegin(const RunInfo& info)
    {
        sqlite3_bind_int64(mInsertRun.get(), 1, info.mStartedMs);
        sqlite3_bind_text(mInsertRun.get(), 2, info.mHost.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(mInsertRun.get(), 3, info.mClientVersion.c_str(), -1, SQLITE_TRANSIENT);
        if (Status status { step(mInsertRun) }; ! status.isOk())
            return status;
        mRunId = sqlite3_last_insert_rowid(mConnection.get());
        return {};
    }

    [[nodiscard]] Status storeRecord(const WireRecord& record) const
    {
        if (! mRunId.has_value())
            return {};
        const Sha256Digest hash { sha256(record.mData) };
        sqlite3_bind_blob(
            mInsertPayload.get(), 1, hash.data(), static_cast<int>(hash.size()), SQLITE_TRANSIENT);
        // A zero-length blob, not NULL, for an empty payload.
        sqlite3_bind_blob(mInsertPayload.get(),
                          2,
                          record.mData.empty() ? "" : static_cast<const void*>(record.mData.data()),
                          static_cast<int>(record.mData.size()),
                          SQLITE_TRANSIENT);
        if (Status status { step(mInsertPayload) }; ! status.isOk())
            return status;

        sqlite3_bind_int64(mInsertWire.get(), 1, *mRunId);
        sqlite3_bind_int64(mInsertWire.get(), 2, static_cast<sqlite3_int64>(record.mSequence));
        sqlite3_bind_int64(mInsertWire.get(), 3, record.mTimestampMs);
        sqlite3_bind_int(mInsertWire.get(), 4, static_cast<int>(record.mDirection));
        sqlite3_bind_blob(mInsertWire.get(), 5, hash.data(), static_cast<int>(hash.size()), SQLITE_TRANSIENT);
        return step(mInsertWire);
    }

    [[nodiscard]] Status storeEnd(std::int64_t endedMs)
    {
        if (! mRunId.has_value())
            return {};
        sqlite3_bind_int64(mEndRun.get(), 1, endedMs);
        sqlite3_bind_int64(mEndRun.get(), 2, *mRunId);
        Status status { step(mEndRun) };
        mRunId.reset();
        return status;
    }

    ConnectionPtr               mConnection;
    StatementPtr                mInsertRun;
    StatementPtr                mEndRun;
    StatementPtr                mInsertPayload;
    StatementPtr                mInsertWire;
    std::optional<std::int64_t> mRunId;
};

WireLog::WireLog(std::filesystem::path path)
    : mPath { std::move(path) }
{
    // Started here, not in the initialiser list, so every member is constructed before it runs.
    mThread = std::thread { [this]()
                            {
                                run();
                            } };
}

WireLog::~WireLog()
{
    // Queued records are flushed before exit, with a bounded wait (persistence.md). After it the
    // writer stops once the batch in hand is done: at most kMaxBatch jobs and one busy timeout.
    static_cast<void>(flush(kCloseWait));
    {
        const std::scoped_lock lock { mMutex };
        mIsStopping = true;
    }
    mWake.notify_all();
    mThread.join();
}

void WireLog::beginRun(RunInfo info)
{
    push(BeginJob { .mInfo = std::move(info) });
}

void WireLog::endRun(std::int64_t endedMs)
{
    push(EndJob { .mEndedMs = endedMs });
}

bool WireLog::append(WireRecord record)
{
    {
        const std::scoped_lock lock { mMutex };
        if (mQueuedRecords >= kMaxQueuedRecords || record.mData.size() > kMaxQueuedBytes - mQueuedBytes)
            return false;
        ++mQueuedRecords;
        mQueuedBytes += record.mData.size();
        mJobs.emplace_back(std::move(record));
    }
    mWake.notify_one();
    return true;
}

void WireLog::push(Job job)
{
    {
        const std::scoped_lock lock { mMutex };
        mJobs.push_back(std::move(job));
    }
    mWake.notify_one();
}

bool WireLog::flush(std::chrono::milliseconds timeout)
{
    std::unique_lock lock { mMutex };
    return mIdle.wait_for(lock,
                          timeout,
                          [this]()
                          {
                              return mJobs.empty() && ! mIsBusy;
                          });
}

std::vector<std::string> WireLog::takeNotices()
{
    const std::scoped_lock lock { mMutex };
    return std::exchange(mNotices, {});
}

void WireLog::notice(std::string text)
{
    const std::scoped_lock lock { mMutex };
    mNotices.push_back(std::move(text));
}

bool WireLog::backOff(std::chrono::milliseconds delay)
{
    std::unique_lock lock { mMutex };
    return ! mWake.wait_for(lock,
                            delay,
                            [this]()
                            {
                                return mIsStopping;
                            });
}

void WireLog::run()
{
    Database                  database;
    Status                    opened;
    std::chrono::milliseconds delay { kFirstBackOff };
    while (true)
    {
        std::vector<std::string> notices;
        opened = database.open(mPath, notices);
        for (std::string& text : notices)
            notice(std::move(text));
        if (! opened.isBusy())
            break;
        // Another instance is migrating: wait for it, never give up on a lock (persistence.md).
        if (! backOff(delay))
            return;
        delay = getNextBackOff(delay);
    }
    bool isFailed { ! opened.isOk() };
    if (isFailed)
    {
        notice("wire log: cannot open " + toUtf8(mPath) + " (" + opened.mMessage
               + "); no records are kept for this run");
    }

    while (true)
    {
        std::vector<Job> jobs;
        {
            std::unique_lock lock { mMutex };
            mWake.wait(lock,
                       [this]()
                       {
                           return mIsStopping || ! mJobs.empty();
                       });
            if (mIsStopping || mJobs.empty())
                return;
            const std::size_t count { std::min(mJobs.size(), kMaxBatch) };
            for (std::size_t i { 0 }; i < count; ++i)
            {
                if (const auto* record { std::get_if<WireRecord>(&mJobs.front()) }; record != nullptr)
                {
                    --mQueuedRecords;
                    mQueuedBytes -= record->mData.size();
                }
                jobs.push_back(std::move(mJobs.front()));
                mJobs.pop_front();
            }
            mIsBusy = true;
        }
        delay = kFirstBackOff;
        while (! isFailed)
        {
            const Status stored { database.store(jobs) };
            if (stored.isOk())
                break;
            if (! stored.isBusy())
            {
                isFailed = true;
                notice("wire log: a write failed (" + stored.mMessage
                       + "); the log stops for the rest of the run");
                break;
            }
            // A lock held by another instance passes; the batch is kept and retried.
            if (! backOff(delay))
                break;
            delay = getNextBackOff(delay);
        }
        {
            const std::scoped_lock lock { mMutex };
            mIsBusy = false;
        }
        mIdle.notify_all();
    }
}
} // namespace muckaii::persistence
