#include "muckaii/core/core.hpp"

#include "muckaii/base/types.hpp"
#include "muckaii/client/client.hpp"
#include "muckaii/persistence/wireLog.hpp"
#include "muckaii/transport/socket.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace muckaii::core
{
namespace
{
// kReadSize bounds each socket read and the reactor's on-heap input buffer.
constexpr std::size_t kReadSize { 65536 };
// kPollTimeout bounds how long the reactor waits before checking queued work again.
constexpr std::chrono::milliseconds kPollTimeout { 250 };
// kMaxReadsPerTurn keeps a flooding peer from starving the FIFO and pending writes.
constexpr int kMaxReadsPerTurn { 16 };
// kStopFlush bounds shutdown writes before unsent lines are reported.
constexpr std::chrono::milliseconds kStopFlush { 2000 };
// kServerClosed is the reason reported when the peer ends the socket.
constexpr std::string_view kServerClosed { "The server closed the connection." };
// kSocketError is the common reason reported for a socket failure.
constexpr std::string_view kSocketError { "The connection failed." };
// kLocalClose is the reason reported after local shutdown.
constexpr std::string_view kLocalClose { "Disconnected." };
// kNoDataDirectory explains why the default wire log cannot be created.
constexpr std::string_view kNoDataDirectory {
    "wire log: no home directory (USERPROFILE or HOME is not set); no records are kept for this run"
};
// kNetworkStartFailure reports failure to initialize the socket subsystem.
constexpr std::string_view kNetworkStartFailure { "The network could not be started." };
// kConnectFailurePrefix begins the notice for an unsuccessful TCP connection.
constexpr std::string_view kConnectFailurePrefix { "Cannot connect to " };
// kDroppedRecordSuffix explains a wire-log queue drop.
constexpr std::string_view kDroppedRecordSuffix { " records dropped here; the writer fell behind" };
// kSingleLineCount selects singular wording for one unsent line.
constexpr std::size_t kSingleLineCount { 1 };
// kUnsentSingleLine describes one line that did not reach the socket.
constexpr std::string_view kUnsentSingleLine { " typed line was" };
// kUnsentMultipleLines describes multiple lines that did not reach the socket.
constexpr std::string_view kUnsentMultipleLines { " typed lines were" };
// kUnsentNoticeSuffix separates the line count from its failure reason.
constexpr std::string_view kUnsentNoticeSuffix { " not sent: " };

// getWallMs returns the system-clock time in milliseconds since its epoch.
std::int64_t getWallMs() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// toBytes copies the low byte of each character into an owned byte sequence.
types::Bytes toBytes(std::string_view text)
{
    types::Bytes bytes;
    bytes.reserve(text.size());
    for (const char c : text)
        bytes.push_back(std::byte { static_cast<std::uint8_t>(c) });
    return bytes;
}

// getRemaining clamps a deadline duration to zero once it has elapsed.
std::chrono::milliseconds getRemaining(std::chrono::steady_clock::time_point deadline) noexcept
{
    const auto left { deadline - std::chrono::steady_clock::now() };
    return std::max(std::chrono::duration_cast<std::chrono::milliseconds>(left),
                    std::chrono::milliseconds { 0 });
}

// makeDroppedNotice describes the number of records refused by the wire-log queue.
std::string makeDroppedNotice(std::uint64_t count)
{
    return std::string { persistence::kWireLogNoticePrefix } + std::to_string(count)
           + std::string { kDroppedRecordSuffix };
}

// makeUnsentNotice describes typed lines that shutdown could not send.
std::string makeUnsentNotice(std::size_t count, std::string_view reason)
{
    const std::string_view lineWord { count == kSingleLineCount ? kUnsentSingleLine : kUnsentMultipleLines };
    return std::to_string(count) + std::string { lineWord } + std::string { kUnsentNoticeSuffix }
           + std::string { reason };
}
} // namespace

// Reactor owns the socket, session, and wire-log handover for one connection.
class Core::Reactor
{
public:
    // Reactor moves the credentials into its session and opens the configured wire log.
    explicit Reactor(Core& core)
        : mCore { core }
        , mWake { *core.mWake }
        , mSession { core.mConfig.mCredentials, core.mConfig.mColumns, core.mConfig.mIsClientWrap }
    {
        // The session holds the only copy the core needs.
        session::wipeSecret(core.mConfig.mCredentials.mAccount);
        session::wipeSecret(core.mConfig.mCredentials.mPassword);
        std::optional<std::filesystem::path> path { core.mConfig.mWireLogPath };
        if (! path.has_value() && core.mConfig.mIsDefaultWireLog)
        {
            path                    = persistence::findDefaultDatabasePath();
            mIsMissingDataDirectory = ! path.has_value();
        }
        if (path.has_value())
            mWireLog = std::make_unique<persistence::WireLog>(*path);
    }

    // run connects, delivers events, and closes the single connection.
    void run()
    {
        const transport::Network network;
        mSession.onConnecting(mBatch);
        if (mIsMissingDataDirectory)
            mBatch.emplace_back(session::NoticeEvent { .mText = std::string { kNoDataDirectory } });
        deliver();

        if (! network.isReady())
        {
            close(std::string { kNetworkStartFailure });
            return;
        }

        const std::string&              host { mCore.mConfig.mHost };
        const std::uint16_t             port { mCore.mConfig.mPort };
        const std::chrono::milliseconds timeout { mCore.mConfig.mConnectTimeout };
        transport::ConnectResult        connected { transport::connectTcp(
            host, port, timeout, mCore.mWake, mCore.mIsStopping) };
        if (connected.mIsCancelled)
        {
            close(std::string { kLocalClose });
            return;
        }
        if (! connected.mConnection.has_value())
        {
            close(std::string { kConnectFailurePrefix } + mCore.mConfig.mHost + ": " + connected.mError);
            return;
        }
        mConnection = std::move(*connected.mConnection);

        // The log opens with the connection, before the first byte (persistence.md).
        mStartedMs     = getWallMs();
        mStartedSteady = std::chrono::steady_clock::now();
        mHasRun        = true;
        if (mWireLog)
        {
            persistence::RunInfo info;
            info.mHost          = mCore.mConfig.mHost;
            info.mClientVersion = mCore.mConfig.mClientVersion;
            info.mStartedMs     = mStartedMs;
            mWireLog->beginRun(info);
        }
        mSession.onConnected(mBatch);
        deliver();

        const std::string reason { loop() };
        mConnection.close();
        close(reason);
        if (mWireLog)
            mWireLog->endRun(getRecordMs());
    }

private:
    using LogDirection = persistence::WireDirection;
    using LogBytes     = std::span<const std::byte>;

    // loop handles socket readiness and FIFO work until the connection ends.
    std::string loop()
    {
        while (true)
        {
            const bool                  willWrite { mIsWriting || mSession.hasOutbound() };
            const transport::PollResult ready { transport::pollSockets(
                mConnection, willWrite, &mWake, kPollTimeout) };
            if (ready.mIsFailed)
                return std::string { kSocketError };

            if (ready.mIsReadable)
            {
                if (types::OptString closed { readSome() }; closed.has_value())
                    return *closed;
            }
            else if (ready.mHasError)
            {
                return std::string { kSocketError };
            }

            if (ready.mIsWoken)
                mWake.drain();
            // Checked after the read, which may have spent time in the sink: a stop asked for
            // meanwhile is honoured before the FIFO is touched, by flushOnStop alone.
            if (mCore.mIsStopping.load())
            {
                flushOnStop();
                return std::string { kLocalClose };
            }
            drainFifo();

            if (types::OptString failed { writeQueue() }; failed.has_value())
                return *failed;

            collectAnnotations();
            collectNotices();
            deliver();
        }
    }

    // readSome logs and decodes up to kMaxReadsPerTurn socket reads.
    types::OptString readSome()
    {
        for (int i { 0 }; i < kMaxReadsPerTurn; ++i)
        {
            const transport::IoResult result { mConnection.read(mBuffer) };
            if (result.mStatus == transport::IoStatus::kWouldBlock)
                return std::nullopt;
            if (result.mStatus == transport::IoStatus::kClosed)
                return std::string { kServerClosed };
            if (result.mStatus == transport::IoStatus::kError)
                return std::string { kSocketError };

            const std::span<const std::byte> bytes { std::span { mBuffer }.first(result.mCount) };
            record(persistence::WireDirection::kReceived, bytes);
            mSession.onReceived(bytes, mBatch);
            collectAnnotations();
            deliver();
        }
        return std::nullopt;
    }

    // drainFifo applies frontend commands to the session in their queued order.
    void drainFifo()
    {
        std::vector<Command> commands;
        {
            const std::scoped_lock lock { mCore.mMutex };
            commands.swap(mCore.mFifo);
        }
        for (const Command& command : commands)
        {
            if (const auto* columns { std::get_if<ColumnsCommand>(&command) }; columns != nullptr)
            {
                mSession.setColumns(columns->mColumns);
                continue;
            }
            // The 7-bit filter is the client's last step.
            const SubmitCommand& submit { std::get<SubmitCommand>(command) };
            mSession.queueLine(client::toSevenBit(submit.mText), submit.mIsSensitive);
        }
    }

    // writeQueue sends session output in order and records each successful socket write.
    types::OptString writeQueue()
    {
        while (mIsWriting || mSession.hasOutbound())
        {
            if (! mIsWriting)
            {
                mWriting   = mSession.takeOutbound();
                mLogged    = session::getLoggedBytes(mWriting);
                mWritten   = 0;
                mIsWriting = true;
            }
            const std::span<const std::byte> rest { std::span { mWriting.mBytes }.subspan(mWritten) };
            const transport::IoResult        result { mConnection.write(rest) };
            if (result.mStatus == transport::IoStatus::kWouldBlock)
                return std::nullopt;
            if (result.mStatus != transport::IoStatus::kDone)
                return std::string { kSocketError };
            if (result.mCount == 0)
                return std::nullopt;

            record(persistence::WireDirection::kSent, std::span { mLogged }.subspan(mWritten, result.mCount));
            mWritten += result.mCount;
            if (mWritten < mWriting.mBytes.size())
                continue;

            if (const std::string_view note { session::getMaskNote(mWriting.mMask) }; ! note.empty())
            {
                record(persistence::WireDirection::kAnnotation, toBytes(note));
                // A masked send may be a credential: its clear bytes are needed no more.
                session::wipeSecret(mWriting.mBytes);
            }
            mIsWriting = false;
            mLogged.clear();
        }
        return std::nullopt;
    }

    // flushOnStop writes already-submitted lines within the bounded shutdown interval.
    void flushOnStop()
    {
        drainFifo();
        const auto deadline { std::chrono::steady_clock::now() + kStopFlush };
        while (mIsWriting || mSession.hasOutbound())
        {
            if (writeQueue().has_value())
                break;
            const std::chrono::milliseconds left { getRemaining(deadline) };
            if (left.count() == 0 || ! (mIsWriting || mSession.hasOutbound()))
                break;
            const transport::PollResult ready { transport::pollSockets(mConnection, true, nullptr, left) };
            if (ready.mIsFailed || ready.mHasError)
                break;
            // Bytes arriving meanwhile are read and logged as usual, so poll() never reports the
            // same unread input again and the wait cannot spin.
            if (ready.mIsReadable && readSome().has_value())
                break;
        }
        collectAnnotations();
        collectNotices();
        deliver();
    }

    // reportUnsent records and publishes a notice for typed lines that never reached the socket.
    void reportUnsent(std::string_view why)
    {
        std::size_t unsent { mSession.getTypedCount() };
        if (mIsWriting && mWriting.mOrigin == session::Origin::kTyped)
            ++unsent;
        {
            const std::scoped_lock lock { mCore.mMutex };
            const auto             isSubmit = [](const Command& command)
            {
                return std::holds_alternative<SubmitCommand>(command);
            };
            unsent += static_cast<std::size_t>(std::ranges::count_if(mCore.mFifo, isSubmit));
            mCore.mFifo.clear();
        }
        if (unsent == 0)
            return;
        const std::string text { makeUnsentNotice(unsent, why) };
        record(persistence::WireDirection::kAnnotation, toBytes(text));
        mBatch.emplace_back(session::NoticeEvent { .mText = text });
    }

    // close reports pending lines and publishes the final connection state.
    void close(std::string reason)
    {
        reportUnsent(reason);
        mSession.onClosed(std::move(reason), mBatch);
        collectNotices();
        deliver();
    }

    // getRecordMs combines the run start with steady elapsed time to avoid timestamp regressions.
    [[nodiscard]] std::int64_t getRecordMs() const noexcept
    {
        const auto elapsed { std::chrono::steady_clock::now() - mStartedSteady };
        return mStartedMs + std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    }

    // record assigns sequence and time, counting any payload the bounded writer queue refuses.
    void record(persistence::WireDirection direction, std::span<const std::byte> bytes)
    {
        if (! mWireLog || ! mHasRun)
            return;
        if (mDropped > 0)
        {
            const std::string note { makeDroppedNotice(mDropped) };
            if (handOver(persistence::WireDirection::kAnnotation, toBytes(note), false))
                mDropped = 0;
        }
        if (handOver(direction, bytes, true))
            return;
        ++mDropped;
        if (! mHasReportedDrop)
        {
            mHasReportedDrop = true;
            mBatch.emplace_back(session::NoticeEvent {
                .mText =
                    std::string { persistence::kWireLogNoticePrefix }
                    + "the writer has fallen behind; records are being dropped, each counted in the log" });
        }
    }

    // handOver submits one timestamped record and applies the requested drop-sequence policy.
    [[nodiscard]] bool handOver(LogDirection direction, LogBytes bytes, bool usesDropSequence)
    {
        persistence::WireRecord record {};
        record.mSequence    = mSequence;
        record.mTimestampMs = getRecordMs();
        record.mData.assign(bytes.begin(), bytes.end());
        record.mDirection = direction;
        const bool isTaken { mWireLog->append(std::move(record)) };
        if (isTaken || usesDropSequence)
            ++mSequence;
        return isTaken;
    }

    // collectAnnotations transfers session annotations into the wire log.
    void collectAnnotations()
    {
        for (const std::string& note : mSession.takeAnnotations())
            record(persistence::WireDirection::kAnnotation, toBytes(note));
    }

    // collectNotices transfers writer notices into the next event batch.
    void collectNotices()
    {
        if (! mWireLog)
            return;
        for (std::string& text : mWireLog->takeNotices())
            mBatch.emplace_back(session::NoticeEvent { .mText = std::move(text) });
    }

    // deliver publishes the input-sensitivity verdict before sending the event batch.
    void deliver()
    {
        // Published before the batch, so a frontend never holds a newer flow than this verdict.
        mCore.mIsInputSensitive.store(mSession.isLineSensitive());
        if (mBatch.empty())
            return;
        if (mCore.mConfig.mSink)
            mCore.mConfig.mSink(std::move(mBatch));
        mBatch.clear();
    }

    std::array<std::byte, kReadSize>      mBuffer {};
    Core&                                 mCore;
    const transport::WakeSocket&          mWake;
    session::Session                      mSession;
    std::unique_ptr<persistence::WireLog> mWireLog;
    transport::TcpConnection              mConnection;
    std::vector<session::SessionEvent>    mBatch;
    session::Outbound                     mWriting; // the send being written, when mIsWriting
    types::Bytes                          mLogged;  // mWriting as the wire log stores it
    std::chrono::steady_clock::time_point mStartedSteady;
    std::size_t                           mWritten {};
    std::uint64_t                         mSequence {};
    std::uint64_t                         mDropped {};
    std::int64_t                          mStartedMs {};
    bool                                  mIsWriting {};
    bool                                  mHasRun {}; // connected, and the log's run begun
    bool                                  mHasReportedDrop {};
    bool                                  mIsMissingDataDirectory {};
};

Core::Core(Key /*key*/, Config config)
    : mConfig { std::move(config) }
{
}

Core::~Core()
{
    stop();
}

bool Core::start()
{
    // Single use: one connection per Core.
    if (mHasStarted)
        return false;
    mHasStarted = true;
    // The socket library stays started for the Core's life: the wake socket outlives the thread.
    mNetwork = std::make_unique<transport::Network>();
    std::optional<transport::WakeSocket> wake { transport::WakeSocket::open() };
    if (! wake.has_value())
        return false;
    mWake   = std::make_shared<const transport::WakeSocket>(std::move(*wake));
    mThread = std::thread { &Core::run, this };
    return true;
}

bool Core::submitLine(std::string text)
{
    const bool isSensitive { mIsInputSensitive.load() };
    push(SubmitCommand { .mText = std::move(text), .mIsSensitive = isSensitive });
    return isSensitive;
}

void Core::setColumns(int columns)
{
    push(ColumnsCommand { .mColumns = columns });
}

void Core::stop()
{
    if (! mThread.joinable())
        return;
    mIsStopping.store(true);
    mWake->wake();
    mThread.join();
}

void Core::push(Command command)
{
    {
        const std::scoped_lock lock { mMutex };
        mFifo.push_back(std::move(command));
    }
    if (mWake)
        mWake->wake();
}

void Core::run()
{
    // The reactor (with its 64 KiB read buffer) lives on the heap, not the thread's stack.
    const std::unique_ptr<Reactor> reactor { std::make_unique<Reactor>(*this) };
    reactor->run();
}

Builder& Builder::setHost(std::string host)
{
    mConfig.mHost = std::move(host);
    return *this;
}

Builder& Builder::setPort(std::uint16_t port)
{
    mConfig.mPort = port;
    return *this;
}

Builder& Builder::setCredentials(const session::Credentials& credentials)
{
    mConfig.mCredentials = credentials;
    return *this;
}

Builder& Builder::setColumns(int columns)
{
    mConfig.mColumns = columns;
    return *this;
}

Builder& Builder::setClientWrap(bool isClientWrap)
{
    mConfig.mIsClientWrap = isClientWrap;
    return *this;
}

Builder& Builder::setWireLog(std::filesystem::path path)
{
    mConfig.mWireLogPath      = std::move(path);
    mConfig.mIsDefaultWireLog = false;
    return *this;
}

Builder& Builder::setDefaultWireLog()
{
    mConfig.mWireLogPath.reset();
    mConfig.mIsDefaultWireLog = true;
    return *this;
}

Builder& Builder::setClientVersion(std::string version)
{
    mConfig.mClientVersion = std::move(version);
    return *this;
}

Builder& Builder::setConnectTimeout(std::chrono::milliseconds timeout)
{
    mConfig.mConnectTimeout = timeout;
    return *this;
}

Builder& Builder::setSink(EventSink sink)
{
    mConfig.mSink = std::move(sink);
    return *this;
}

std::unique_ptr<Core> Builder::build()
{
    return std::make_unique<Core>(Core::Key {}, std::move(mConfig));
}
} // namespace muckaii::core
