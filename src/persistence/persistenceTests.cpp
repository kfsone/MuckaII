#include "muckaii/base/types.hpp"
#include "muckaii/persistence/sha256.hpp"
#include "muckaii/persistence/wireLog.hpp"

#include <gtest/gtest.h>

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace muckaii::persistence;
namespace types = muckaii::types;
using namespace std::chrono_literals;

namespace
{
// toHex renders a digest as lowercase hexadecimal.
std::string toHex(const Sha256Digest& digest)
{
    // kHex maps a digest nibble to its lowercase hexadecimal character.
    static constexpr std::string_view kHex { "0123456789abcdef" };
    std::string                       hex;
    for (const std::uint8_t byte : digest)
    {
        hex.push_back(kHex[byte >> 4U]);
        hex.push_back(kHex[byte & 0x0FU]);
    }
    return hex;
}

// bytes converts test text into its byte representation.
types::Bytes bytes(std::string_view text)
{
    types::Bytes out;
    for (const char c : text)
        out.push_back(std::byte { static_cast<std::uint8_t>(c) });
    return out;
}

// makeTempDatabase clears stale SQLite files and returns a test path.
std::filesystem::path makeTempDatabase(const std::string& name)
{
    const auto testRoot { std::filesystem::temp_directory_path() / "muckaiiPersistenceTests" };
    const std::filesystem::path directory { testRoot };
    std::filesystem::create_directories(directory);
    const std::filesystem::path path { directory / (name + ".db") };
    std::error_code             error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(directory / (name + ".db-wal"), error);
    std::filesystem::remove(directory / (name + ".db-shm"), error);
    return path;
}

// Row holds the selected fields of a wire-log record.
struct Row
{
    std::int64_t  mRun {};
    std::int64_t  mSeq {};
    WireDirection mDirection { WireDirection::kReceived };
    std::string   mData;
};

// readWireLog reads stored rows in run and sequence order.
std::vector<Row> readWireLog(const std::filesystem::path& path)
{
    sqlite3* database {};
    EXPECT_EQ(sqlite3_open_v2(toUtf8(path).c_str(), &database, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
    sqlite3_stmt* query {};
    // kReadWireLogSql selects the fields used to verify persisted wire records.
    constexpr const char* kReadWireLogSql {
        "SELECT run, seq, direction, data FROM wire_log ORDER BY run, seq;"
    };
    EXPECT_EQ(sqlite3_prepare_v2(database, kReadWireLogSql, -1, &query, nullptr), SQLITE_OK);
    std::vector<Row> rows;
    while (sqlite3_step(query) == SQLITE_ROW)
    {
        const auto*       data { static_cast<const char*>(sqlite3_column_blob(query, 3)) };
        const auto        length { static_cast<std::size_t>(sqlite3_column_bytes(query, 3)) };
        const std::string payload { data != nullptr ? data : "", length };
        rows.push_back(Row { .mRun       = sqlite3_column_int64(query, 0),
                             .mSeq       = sqlite3_column_int64(query, 1),
                             .mDirection = static_cast<WireDirection>(sqlite3_column_int(query, 2)),
                             .mData      = payload });
    }
    sqlite3_finalize(query);
    sqlite3_close(database);
    return rows;
}

// countRows returns the first integer from a read-only database query.
int countRows(const std::filesystem::path& path, const char* sql)
{
    sqlite3* database {};
    EXPECT_EQ(sqlite3_open_v2(toUtf8(path).c_str(), &database, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
    sqlite3_stmt* query {};
    EXPECT_EQ(sqlite3_prepare_v2(database, sql, -1, &query, nullptr), SQLITE_OK);
    const int count { sqlite3_step(query) == SQLITE_ROW ? sqlite3_column_int(query, 0) : -1 };
    sqlite3_finalize(query);
    sqlite3_close(database);
    return count;
}

// makeRecord builds a received record with sequence-based test time.
WireRecord makeRecord(std::uint64_t sequence, std::string_view data)
{
    return WireRecord { .mSequence    = sequence,
                        .mTimestampMs = static_cast<std::int64_t>(sequence),
                        .mData        = bytes(data),
                        .mDirection   = WireDirection::kReceived };
}

// makeRecord constructs a wire record with explicit timestamp and direction fields.
WireRecord
makeRecord(std::uint64_t sequence, std::int64_t timestamp, std::string_view data, WireDirection direction)
{
    WireRecord record;
    record.mSequence    = sequence;
    record.mTimestampMs = timestamp;
    record.mData        = bytes(data);
    record.mDirection   = direction;
    return record;
}

// Another connection holding the database's write lock, as a second MuckaII instance does while it
// writes.
// WriteLock holds an immediate SQLite write transaction for contention tests.
class WriteLock
{
public:
    // WriteLock opens path and begins an immediate transaction.
    explicit WriteLock(const std::filesystem::path& path)
    {
        EXPECT_EQ(sqlite3_open_v2(toUtf8(path).c_str(), &mDatabase, SQLITE_OPEN_READWRITE, nullptr),
                  SQLITE_OK);
        EXPECT_EQ(sqlite3_exec(mDatabase, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr), SQLITE_OK);
    }

    // ~WriteLock releases the held database lock.
    ~WriteLock()
    {
        release();
    }

    WriteLock(const WriteLock&)            = delete;
    WriteLock& operator=(const WriteLock&) = delete;
    WriteLock(WriteLock&&)                 = delete;
    WriteLock& operator=(WriteLock&&)      = delete;

    // release rolls back the lock transaction and closes its database.
    void release()
    {
        if (mDatabase == nullptr)
            return;
        sqlite3_exec(mDatabase, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_close(mDatabase);
        mDatabase = nullptr;
    }

private:
    sqlite3* mDatabase {};
};

// A database whose schema exists, so a lock taken on it is the only obstacle.
// createSchema initializes an empty wire-log database before lock tests.
void createSchema(const std::filesystem::path& path)
{
    WireLog log { path };
    ASSERT_TRUE(log.flush(5s));
    ASSERT_TRUE(log.takeNotices().empty());
}
} // namespace

// Sha256_MatchesTheFipsExamples checks the digest matches representative FIPS SHA-256 examples.
TEST(Sha256, MatchesTheFipsExamples)
{
    EXPECT_EQ(toHex(sha256(bytes(""))), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(toHex(sha256(bytes("abc"))),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(toHex(sha256(bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(toHex(sha256(bytes(std::string(1000000, 'a')))),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// WireLog_StoresRecordsInOrderWithPayloadsOnce checks records retain sequence order while identical payloads are stored once.
TEST(WireLog, StoresRecordsInOrderWithPayloadsOnce)
{
    const std::filesystem::path path { makeTempDatabase("ordered") };
    {
        WireLog log { path };
        log.beginRun(RunInfo { .mHost = "127.0.0.1", .mClientVersion = "test", .mStartedMs = 1 });
        EXPECT_TRUE(log.append(makeRecord(0, 2, "room\r\n", WireDirection::kReceived)));
        EXPECT_TRUE(log.append(makeRecord(1, 3, "n\r\n", WireDirection::kSent)));
        EXPECT_TRUE(log.append(makeRecord(2, 4, "room\r\n", WireDirection::kReceived)));
        EXPECT_TRUE(log.append(makeRecord(3, 5, "a note", WireDirection::kAnnotation)));
        log.endRun(6);
        ASSERT_TRUE(log.flush(5s));
        EXPECT_TRUE(log.takeNotices().empty());
    }

    const std::vector<Row> rows { readWireLog(path) };
    ASSERT_EQ(rows.size(), 4U);
    EXPECT_EQ(rows[0].mData, "room\r\n");
    EXPECT_EQ(rows[1].mDirection, WireDirection::kSent);
    EXPECT_EQ(rows[1].mData, "n\r\n");
    EXPECT_EQ(rows[2].mData, "room\r\n");
    EXPECT_EQ(rows[3].mDirection, WireDirection::kAnnotation);
    for (std::size_t i { 0 }; i < rows.size(); ++i)
        EXPECT_EQ(rows[i].mSeq, static_cast<std::int64_t>(i));
    EXPECT_EQ(countRows(path, "SELECT COUNT(*) FROM payload;"), 3);
    EXPECT_EQ(countRows(path, "SELECT ended_ms FROM run;"), 6);
    EXPECT_EQ(countRows(path, "SELECT COUNT(*) FROM schema_step;"), kSchemaSteps);
}

// WireLog_TwoRunsShareOneDatabase checks separate runs share one wire-log database.
TEST(WireLog, TwoRunsShareOneDatabase)
{
    const std::filesystem::path path { makeTempDatabase("shared") };
    for (int run { 0 }; run < 2; ++run)
    {
        WireLog log { path };
        log.beginRun(RunInfo { .mHost = "h", .mClientVersion = "test", .mStartedMs = run });
        EXPECT_TRUE(log.append(makeRecord(0, "x")));
        log.endRun(2);
        ASSERT_TRUE(log.flush(5s));
    }
    EXPECT_EQ(countRows(path, "SELECT COUNT(*) FROM run;"), 2);
    EXPECT_EQ(countRows(path, "SELECT COUNT(*) FROM wire;"), 2);
    EXPECT_EQ(countRows(path, "SELECT COUNT(*) FROM schema_step;"), kSchemaSteps);
}

// WireLog_AnOpenFailureIsReportedOnceAndNothingIsKept checks an open failure is reported once and stores no records.
TEST(WireLog, AnOpenFailureIsReportedOnceAndNothingIsKept)
{
    const std::filesystem::path directory { makeTempDatabase("notADatabase") };
    std::filesystem::create_directories(directory);
    WireLog log { directory };
    log.beginRun(RunInfo { .mHost = "h", .mClientVersion = "test", .mStartedMs = 0 });
    EXPECT_TRUE(log.append(makeRecord(0, "x")));
    ASSERT_TRUE(log.flush(5s));
    const std::vector<std::string> notices { log.takeNotices() };
    ASSERT_EQ(notices.size(), 1U);
    EXPECT_NE(notices[0].find("cannot open"), std::string::npos);
    EXPECT_TRUE(log.takeNotices().empty());
}

// SQLite is handed UTF-8, never the ANSI code page.
// WireLog_OpensAPathOutsideTheAnsiCodePage checks database paths are passed to SQLite as UTF-8.
TEST(WireLog, OpensAPathOutsideTheAnsiCodePage)
{
    // U+00E9 and U+4E2D: the second is in no Western ANSI code page.
    const std::u8string name { u8"caf\u00E9 \u4E2D" };
    const auto          testRoot { std::filesystem::temp_directory_path() / "muckaiiPersistenceTests" };
    const std::filesystem::path directory { testRoot / name };
    std::error_code             error;
    std::filesystem::remove_all(directory, error);
    const std::filesystem::path path { directory / "log.db" };
    {
        WireLog log { path };
        log.beginRun(RunInfo { .mHost = "h", .mClientVersion = "test", .mStartedMs = 0 });
        EXPECT_TRUE(log.append(makeRecord(0, "x")));
        log.endRun(1);
        ASSERT_TRUE(log.flush(5s));
        EXPECT_TRUE(log.takeNotices().empty());
    }
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(countRows(path, "SELECT COUNT(*) FROM wire;"), 1);
}

// A lock held by another instance delays the log; it never stops it (persistence.md "Wire log").
// WireLog_ALockedDatabaseIsRetriedNotAbandoned checks database lock contention retries the queued batch.
TEST(WireLog, ALockedDatabaseIsRetriedNotAbandoned)
{
    const std::filesystem::path path { makeTempDatabase("busy") };
    createSchema(path);
    {
        WriteLock lock { path };
        WireLog   log { path };
        log.beginRun(RunInfo { .mHost = "h", .mClientVersion = "test", .mStartedMs = 0 });
        for (std::uint64_t i { 0 }; i < 10; ++i)
            EXPECT_TRUE(log.append(makeRecord(i, "x")));
        // Long enough for several failed attempts (SQLite's own wait is 250 ms).
        EXPECT_FALSE(log.flush(1500ms));
        EXPECT_TRUE(log.takeNotices().empty());
        lock.release();
        log.endRun(1);
        ASSERT_TRUE(log.flush(10s));
        EXPECT_TRUE(log.takeNotices().empty());
    }
    EXPECT_EQ(countRows(path, "SELECT COUNT(*) FROM wire;"), 10);
    EXPECT_EQ(countRows(path, "SELECT ended_ms FROM run;"), 1);
}

// The queue is bounded: past kMaxQueuedRecords a record is refused at once, never waited for.
// WireLog_AFullQueueRefusesRecordsWithoutBlocking checks the record queue rejects overflow without waiting.
TEST(WireLog, AFullQueueRefusesRecordsWithoutBlocking)
{
    const std::filesystem::path path { makeTempDatabase("full") };
    createSchema(path);
    WriteLock lock { path };
    WireLog   log { path };
    log.beginRun(RunInfo { .mHost = "h", .mClientVersion = "test", .mStartedMs = 0 });
    std::size_t refused { 0 };
    const auto  started { std::chrono::steady_clock::now() };
    for (std::uint64_t i { 0 }; i < kMaxQueuedRecords + 2000; ++i)
    {
        if (! log.append(makeRecord(i, "x")))
            ++refused;
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, 2s);
    EXPECT_GT(refused, 0U);
    lock.release();
    ASSERT_TRUE(log.flush(30s));
    EXPECT_EQ(static_cast<std::size_t>(countRows(path, "SELECT COUNT(*) FROM wire;")),
              kMaxQueuedRecords + 2000 - refused);
}

// Closing waits a bounded time even when the writer cannot make progress.
// WireLog_ClosingIsBoundedWhileTheDatabaseIsLocked checks destruction remains bounded while the database is locked.
TEST(WireLog, ClosingIsBoundedWhileTheDatabaseIsLocked)
{
    const std::filesystem::path path { makeTempDatabase("closing") };
    createSchema(path);
    WriteLock  lock { path };
    const auto started { std::chrono::steady_clock::now() };
    {
        WireLog log { path };
        log.beginRun(RunInfo { .mHost = "h", .mClientVersion = "test", .mStartedMs = 0 });
        for (std::uint64_t i { 0 }; i < 5000; ++i)
            static_cast<void>(log.append(makeRecord(i, "x")));
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, 6s);
}

// The byte bound refuses records long before the record bound when they are large.
// WireLog_TheQueueIsBoundedInBytes checks payload bytes enforce the wire-log queue bound.
TEST(WireLog, TheQueueIsBoundedInBytes)
{
    const std::filesystem::path path { makeTempDatabase("bytes") };
    createSchema(path);
    WriteLock lock { path };
    WireLog   log { path };
    const std::string big(std::size_t { 1024 } * 1024, 'x');
    std::size_t accepted { 0 };
    for (std::uint64_t i { 0 }; i < 100; ++i)
    {
        if (log.append(makeRecord(i, big)))
            ++accepted;
    }
    EXPECT_EQ(accepted, kMaxQueuedBytes / big.size());
    lock.release();
    ASSERT_TRUE(log.flush(30s));
}

// The schema's references hold: with foreign keys on (WireLog turns them on and reads the pragma
// back, refusing to log otherwise), a wire row naming no run or no payload is refused.
// WireLog_TheSchemaEnforcesItsReferences checks foreign keys reject wire rows without a run or payload.
TEST(WireLog, TheSchemaEnforcesItsReferences)
{
    const std::filesystem::path path { makeTempDatabase("references") };
    createSchema(path);
    sqlite3* database {};
    ASSERT_EQ(sqlite3_open_v2(toUtf8(path).c_str(), &database, SQLITE_OPEN_READWRITE, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(database, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr), SQLITE_OK);
    EXPECT_NE(
        sqlite3_exec(database,
                     "INSERT INTO wire (run, seq, ts_ms, direction, payload) VALUES (99, 0, 0, 0, x'00');",
                     nullptr,
                     nullptr,
                     nullptr),
        SQLITE_OK);
    sqlite3_close(database);
    EXPECT_EQ(countRows(path, "SELECT COUNT(*) FROM wire;"), 0);
}

// DataDirectory_TheDefaultDatabaseIsInDotMuckaii checks the default database resolves under the user data directory.
TEST(DataDirectory, TheDefaultDatabaseIsInDotMuckaii)
{
    const std::optional<std::filesystem::path> path { findDefaultDatabasePath() };
    ASSERT_TRUE(path.has_value()) << "USERPROFILE or HOME is not set";
    EXPECT_EQ(path->filename(), "muckaii.db");
    EXPECT_EQ(path->parent_path().filename(), ".muckaii");
}
