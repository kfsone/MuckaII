#pragma once

#include "muckaii/base/types.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

// WireLog stores all sent and received bytes on a background SQLite writer; the caller assigns sequence and time.
namespace muckaii::persistence
{
// WireDirection identifies the stored direction of one wire-log record.
enum class WireDirection : std::uint8_t
{
    kReceived   = 0,
    kSent       = 1,
    kAnnotation = 2,
};

// WireRecord carries one sequence-numbered wire-log payload.
struct WireRecord
{
    std::uint64_t mSequence {};
    std::int64_t  mTimestampMs {};
    types::Bytes  mData;
    WireDirection mDirection { WireDirection::kReceived };
};

// RunInfo identifies the connection recorded by a wire-log run.
struct RunInfo
{
    std::string  mHost;
    std::string  mClientVersion;
    std::int64_t mStartedMs {};
};

// kSchemaSteps counts the wire-log schema migrations supported by this version.
constexpr int kSchemaSteps { 1 };
// kWireLogNoticePrefix identifies status notices from the asynchronous wire-log writer.
constexpr std::string_view kWireLogNoticePrefix { "wire log: " };
// kSqliteOutOfMemoryMessage is used when SQLite cannot allocate an error message.
constexpr std::string_view kSqliteOutOfMemoryMessage { "out of memory" };
// kSqliteUnknownErrorMessage is used when SQLite returns no diagnostic text.
constexpr std::string_view kSqliteUnknownErrorMessage { "unknown error" };

// kDatabaseName is the standard SQLite file name under the user data directory.
constexpr std::string_view kDatabaseName { "muckaii.db" };

// kMaxQueuedRecords bounds queued records before the core drops and counts another record.
constexpr std::size_t kMaxQueuedRecords { 16384 };

// kMaxQueuedBytes bounds payload storage even when a small number of records are large.
constexpr std::size_t kMaxQueuedBytes { std::size_t { 64 } * 1024 * 1024 };

// findDefaultDatabasePath resolves the standard user database path, or returns empty without a home.
[[nodiscard]] std::optional<std::filesystem::path> findDefaultDatabasePath();

// toUtf8 converts a filesystem path to the encoding SQLite accepts on every platform.
[[nodiscard]] std::string toUtf8(const std::filesystem::path& path);

// WireLog stores wire records on one background writer thread.
class WireLog
{
public:
    // WireLog starts a writer for path and reports any open failure through takeNotices().
    explicit WireLog(std::filesystem::path path);

    // ~WireLog waits boundedly, then stops after the current batch.
    ~WireLog();
    WireLog(const WireLog&)            = delete;
    WireLog& operator=(const WireLog&) = delete;
    WireLog(WireLog&&)                 = delete;
    WireLog& operator=(WireLog&&)      = delete;

    // beginRun queues the start metadata for a new connection.
    void beginRun(RunInfo info);
    // endRun queues the connection end timestamp.
    void endRun(std::int64_t endedMs);

    // append queues a record or returns false when either queue bound would be exceeded.
    [[nodiscard]] bool append(WireRecord record);

    // flush waits up to timeout for all queued jobs to be stored.
    [[nodiscard]] bool flush(std::chrono::milliseconds timeout);

    // takeNotices transfers pending writer reports to the caller.
    [[nodiscard]] std::vector<std::string> takeNotices();

private:
    // BeginJob queues the metadata that opens a wire-log run.
    struct BeginJob
    {
        RunInfo mInfo;
    };
    // EndJob queues the timestamp that closes a wire-log run.
    struct EndJob
    {
        std::int64_t mEndedMs {};
    };
    using Job = std::variant<BeginJob, WireRecord, EndJob>;

    class Database;

    // run opens the database and drains queued jobs on the writer thread.
    void run();
    // notice queues a one-time report for the core thread.
    void notice(std::string text);
    // push queues a non-record job and wakes the writer.
    void push(Job job);

    // backOff waits for delay and returns false if shutdown interrupts the wait.
    [[nodiscard]] bool backOff(std::chrono::milliseconds delay);

    std::filesystem::path    mPath;
    std::mutex               mMutex;
    std::condition_variable  mWake;
    std::condition_variable  mIdle;
    std::deque<Job>          mJobs;
    std::vector<std::string> mNotices;
    std::thread              mThread;
    std::size_t              mQueuedRecords {};
    std::size_t              mQueuedBytes {};
    bool                     mIsStopping {};
    bool                     mIsBusy {};
};
} // namespace muckaii::persistence
