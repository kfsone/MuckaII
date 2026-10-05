#include "muckaii/core/core.hpp"

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
constexpr std::size_t               kReadSize { 65536 };
constexpr std::chrono::milliseconds kPollTimeout { 250 };
// Reads per reactor turn, so a flooding server never starves the FIFO and the writes behind it.
constexpr int kMaxReadsPerTurn { 16 };
// How long stop() keeps writing lines already submitted (bounded, then the rest is reported).
constexpr std::chrono::milliseconds kStopFlush { 2000 };
constexpr std::string_view          kServerClosed { "The server closed the connection." };
constexpr std::string_view          kSocketError { "The connection failed." };
constexpr std::string_view          kLocalClose { "Disconnected." };
constexpr std::string_view          kNoDataDirectory {
    "wire log: no home directory (USERPROFILE or HOME is not set); no records are kept for this run"
};

std::int64_t getWallMs() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::vector<std::byte> toBytes(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char c : text)
        bytes.push_back(std::byte { static_cast<std::uint8_t>(c) });
    return bytes;
}

std::chrono::milliseconds getRemaining(std::chrono::steady_clock::time_point deadline) noexcept
{
    const auto left { deadline - std::chrono::steady_clock::now() };
    return std::max(std::chrono::duration_cast<std::chrono::milliseconds>(left),
                    std::chrono::milliseconds { 0 });
}
} // namespace

// Everything the core thread owns for one connection.
class Core::Reactor
{
public:
    explicit Reactor(Core& core)
        : mCore { core }
        , mWake { *core.mWake }
        , mSession { core.mConfig.mCredentials, core.mConfig.mColumns }
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

    void run()
    {
        const transport::Network network;
        mSession.onConnecting(mBatch);
        if (mIsMissingDataDirectory)
            mBatch.emplace_back(session::NoticeEvent { .mText = std::string { kNoDataDirectory } });
        deliver();

        if (! network.isReady())
        {
            close("The network could not be started.");
            return;
        }

        transport::ConnectResult connected { transport::connectTcp(mCore.mConfig.mHost,
                                                                   mCore.mConfig.mPort,
                                                                   mCore.mConfig.mConnectTimeout,
                                                                   mCore.mWake,
                                                                   mCore.mIsStopping) };
        if (connected.mIsCancelled)
        {
            close(std::string { kLocalClose });
            return;
        }
        if (! connected.mConnection.has_value())
        {
            close("Cannot connect to " + mCore.mConfig.mHost + ": " + connected.mError);
            return;
        }
        mConnection = std::move(*connected.mConnection);

        // The log opens with the connection, before the first byte (persistence.md).
        mStartedMs     = getWallMs();
        mStartedSteady = std::chrono::steady_clock::now();
        mHasRun        = true;
        if (mWireLog)
        {
            mWireLog->beginRun(persistence::RunInfo { .mHost          = mCore.mConfig.mHost,
                                                      .mClientVersion = mCore.mConfig.mClientVersion,
                                                      .mStartedMs     = mStartedMs });
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
    // One reactor turn after another, in architecture.md's order: read and decode; drain the wake
    // socket and the FIFO into the session's queue; write the queue.
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
                if (std::optional<std::string> closed { readSome() }; closed.has_value())
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

            if (std::optional<std::string> failed { writeQueue() }; failed.has_value())
                return *failed;

            collectAnnotations();
            collectNotices();
            deliver();
        }
    }

    // Reads until the socket would block, or kMaxReadsPerTurn reads. Each read is one wire record
    // and one event batch.
    std::optional<std::string> readSome()
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

    // Writes the queue in order. Each socket write is one sent record; a masked send is always its
    // own write, so masking never splits a coalesced write.
    std::optional<std::string> writeQueue()
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

    // Lines submitted before stop() are written, within kStopFlush.
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

    // Typed lines that never reached the socket are said to be lost, never dropped silently.
    void reportUnsent(std::string_view why)
    {
        std::size_t unsent { mSession.getTypedCount() };
        if (mIsWriting && mWriting.mOrigin == session::Origin::kTyped)
            ++unsent;
        {
            const std::scoped_lock lock { mCore.mMutex };
            unsent += static_cast<std::size_t>(
                std::ranges::count_if(mCore.mFifo,
                                      [](const Command& command)
                                      {
                                          return std::holds_alternative<SubmitCommand>(command);
                                      }));
            mCore.mFifo.clear();
        }
        if (unsent == 0)
            return;
        const std::string text { std::to_string(unsent)
                                 + (unsent == 1 ? " typed line was" : " typed lines were")
                                 + " not sent: " + std::string { why } };
        record(persistence::WireDirection::kAnnotation, toBytes(text));
        mBatch.emplace_back(session::NoticeEvent { .mText = text });
    }

    void close(std::string reason)
    {
        reportUnsent(reason);
        mSession.onClosed(std::move(reason), mBatch);
        collectNotices();
        deliver();
    }

    // Record time: the run's wall-clock start plus steady time since, so timestamps never run
    // backwards within a run (persistence.md "What a record is").
    [[nodiscard]] std::int64_t getRecordMs() const noexcept
    {
        const auto elapsed { std::chrono::steady_clock::now() - mStartedSteady };
        return mStartedMs + std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    }

    // Core assigns each record its run sequence and timestamp at handover (architecture.md). A
    // record the writer cannot take is dropped and counted; its sequence number stays used, so the
    // gap shows, and the next record that fits is preceded by an annotation giving the count. The
    // count annotation itself, when refused, uses no sequence number and is tried again later.
    void record(persistence::WireDirection direction, std::span<const std::byte> bytes)
    {
        if (! mWireLog || ! mHasRun)
            return;
        if (mDropped > 0)
        {
            const std::string note { "wire log: " + std::to_string(mDropped)
                                     + " records dropped here; the writer fell behind" };
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
                .mText = "wire log: the writer has fallen behind; records are being dropped, each counted in "
                         "the log" });
        }
    }

    // False when refused; `isSequenceUsedOnDrop` says whether a refusal still uses the number.
    [[nodiscard]] bool handOver(persistence::WireDirection direction,
                                std::span<const std::byte> bytes,
                                bool                       isSequenceUsedOnDrop)
    {
        const bool isTaken { mWireLog->append(
            persistence::WireRecord { .mSequence    = mSequence,
                                      .mTimestampMs = getRecordMs(),
                                      .mData        = std::vector<std::byte> { bytes.begin(), bytes.end() },
                                      .mDirection   = direction }) };
        if (isTaken || isSequenceUsedOnDrop)
            ++mSequence;
        return isTaken;
    }

    void collectAnnotations()
    {
        for (const std::string& note : mSession.takeAnnotations())
            record(persistence::WireDirection::kAnnotation, toBytes(note));
    }

    void collectNotices()
    {
        if (! mWireLog)
            return;
        for (std::string& text : mWireLog->takeNotices())
            mBatch.emplace_back(session::NoticeEvent { .mText = std::move(text) });
    }

    void deliver()
    {
        // Published before the batch, so a frontend never holds a newer flow than this verdict.
        mCore.mIsInputSensitive.store(mSession.isLineSensitive());
        mCore.mIsInputHidden.store(mSession.isInputHidden());
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
    std::vector<std::byte>                mLogged;  // mWriting as the wire log stores it
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
    mThread = std::thread { [this]()
                            {
                                run();
                            } };
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
