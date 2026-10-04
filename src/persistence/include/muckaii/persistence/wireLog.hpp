#pragma once

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

// The wire log (docs/spec/persistence.md "Wire log"): every byte in both directions, stored by one
// background writer thread in SQLite, payloads content-addressed. The caller (core) assigns each
// record its run sequence and timestamp at handover; this class only stores.
namespace muckaii::persistence
{
// Stored values, never renumbered (persistence.md "What a record is").
enum class WireDirection : std::uint8_t
{
    kReceived   = 0,
    kSent       = 1,
    kAnnotation = 2,
};

struct WireRecord
{
    std::uint64_t          mSequence {};
    std::int64_t           mTimestampMs {};
    std::vector<std::byte> mData;
    WireDirection          mDirection { WireDirection::kReceived };
};

struct RunInfo
{
    std::string  mHost;
    std::string  mClientVersion;
    std::int64_t mStartedMs {};
};

// The schema steps this version knows (persistence.md "Schema forward-compatibility").
constexpr int kSchemaSteps { 1 };

// The database file name inside ~/.muckaii/ (persistence.md "Data directory", default: line).
constexpr std::string_view kDatabaseName { "muckaii.db" };

// Records waiting for the writer. A record handed over while this many wait is dropped and counted,
// never waited for: the core thread must not stall (persistence.md "Wire log").
constexpr std::size_t kMaxQueuedRecords { 16384 };

// The same bound on the bytes those records carry, so a few huge reads cannot use unbounded memory.
constexpr std::size_t kMaxQueuedBytes { std::size_t { 64 } * 1024 * 1024 };

// ~/.muckaii/muckaii.db (persistence.md "Data directory"): %USERPROFILE% on Windows, $HOME
// elsewhere. nullopt when that variable is unset or empty.
[[nodiscard]] std::optional<std::filesystem::path> findDefaultDatabasePath();

// UTF-8 text of a path, for SQLite and for messages (never the ANSI code page).
[[nodiscard]] std::string toUtf8(const std::filesystem::path& path);

class WireLog
{
public:
    // Starts the writer thread, which opens (creating if needed) the database at `path`. Never
    // fails here: an open failure is reported through takeNotices() and the log keeps nothing.
    explicit WireLog(std::filesystem::path path);

    // Waits a bounded time for queued records, then stops: the writer finishes the batch in hand
    // and the rest is dropped.
    ~WireLog();
    WireLog(const WireLog&)            = delete;
    WireLog& operator=(const WireLog&) = delete;
    WireLog(WireLog&&)                 = delete;
    WireLog& operator=(WireLog&&)      = delete;

    // Hand-over, from the core thread. Each only takes the lock for a push.
    void beginRun(RunInfo info);
    void endRun(std::int64_t endedMs);

    // False when the record was dropped because kMaxQueuedRecords records, or kMaxQueuedBytes
    // bytes with it, would be waiting.
    [[nodiscard]] bool append(WireRecord record);

    // Waits up to `timeout` for every queued record to be stored. False on timeout.
    [[nodiscard]] bool flush(std::chrono::milliseconds timeout);

    // One-time reports for the terminal: an open failure, a write failure, a newer schema.
    [[nodiscard]] std::vector<std::string> takeNotices();

private:
    struct BeginJob
    {
        RunInfo mInfo;
    };
    struct EndJob
    {
        std::int64_t mEndedMs {};
    };
    using Job = std::variant<BeginJob, WireRecord, EndJob>;

    class Database;

    void run();
    void notice(std::string text);
    void push(Job job);

    // Waits `delay`, or less if stopping. False when stopping.
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
