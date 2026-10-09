#include "muckaii/core/fakeServer/fakeMudServer.hpp"

#include "muckaii/base/types.hpp"
#include "muckaii/mud/golden/golden.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace muckaii::core::fake
{
namespace
{
using namespace std::chrono_literals;

// kAcceptTimeout bounds how long the fake server waits for its single client.
constexpr std::chrono::milliseconds kAcceptTimeout { 10s };
// kExpectTimeout bounds a client-byte expectation.
constexpr std::chrono::milliseconds kExpectTimeout { 10s };
// kPollSlice keeps fake-server waits responsive to shutdown.
constexpr std::chrono::milliseconds kPollSlice { 50ms };
// kReadSize bounds each read from the scripted client.
constexpr std::size_t kReadSize { 4096 };
// kShownBytes limits byte excerpts in expectation errors.
constexpr std::size_t kShownBytes { 48 };
// kListenFailure explains why the fake server has no listening port.
constexpr std::string_view kListenFailure { "cannot listen on 127.0.0.1" };

// toBytes copies each text character into a wire byte.
types::Bytes toBytes(std::string_view text)
{
    types::Bytes bytes;
    for (const char c : text)
        bytes.push_back(std::byte { static_cast<std::uint8_t>(c) });
    return bytes;
}

// describe formats a bounded uppercase hexadecimal excerpt for diagnostics.
std::string describe(std::span<const std::byte> bytes)
{
    // kHex maps a nibble to its uppercase diagnostic digit.
    static constexpr std::string_view kHex { "0123456789ABCDEF" };
    std::string                       text;
    for (std::size_t i { 0 }; i < std::min(bytes.size(), kShownBytes); ++i)
    {
        const auto value { std::to_integer<std::uint8_t>(bytes[i]) };
        if (i > 0)
            text.push_back(' ');
        text.push_back(kHex[value >> 4U]);
        text.push_back(kHex[value & 0x0FU]);
    }
    if (bytes.size() > kShownBytes)
        text.append(" ...");
    return text;
}
} // namespace

ScriptStep send(std::string_view text)
{
    return send(toBytes(text));
}

ScriptStep send(types::Bytes bytes)
{
    return ScriptStep {
        .mBytes = std::move(bytes), .mPause = {}, .mCount = 0, .mKind = ScriptStep::Kind::kSend
    };
}

ScriptStep expect(std::string_view text)
{
    return expect(toBytes(text));
}

ScriptStep expect(types::Bytes bytes)
{
    return ScriptStep {
        .mBytes = std::move(bytes), .mPause = {}, .mCount = 0, .mKind = ScriptStep::Kind::kExpect
    };
}

ScriptStep expectCount(std::size_t count)
{
    return ScriptStep {
        .mBytes = {}, .mPause = {}, .mCount = count, .mKind = ScriptStep::Kind::kExpectCount
    };
}

ScriptStep pause(std::chrono::milliseconds duration)
{
    return ScriptStep { .mBytes = {}, .mPause = duration, .mCount = 0, .mKind = ScriptStep::Kind::kPause };
}

std::vector<ScriptStep> replayCapture(const std::filesystem::path& capture, std::chrono::milliseconds gap)
{
    std::vector<ScriptStep>              steps;
    const mud::golden::OptCaptureRecords records { mud::golden::readCapture(capture) };
    if (! records.has_value())
        return steps;
    for (const mud::golden::CaptureRecord& record : *records)
    {
        if (record.mDirection != mud::golden::Direction::kRx)
            continue;
        steps.push_back(send(record.mPayload));
        steps.push_back(pause(gap));
    }
    return steps;
}

types::Bytes getExpectedBytes(const std::vector<ScriptStep>& script)
{
    types::Bytes bytes;
    for (const ScriptStep& step : script)
    {
        if (step.mKind == ScriptStep::Kind::kExpect)
            bytes.insert(bytes.end(), step.mBytes.begin(), step.mBytes.end());
    }
    return bytes;
}

FakeMudServer::FakeMudServer(std::vector<ScriptStep> script)
    : mListener { transport::loopback::TcpListener::listen() }
    , mScript { std::move(script) }
{
    if (! mListener.has_value())
    {
        mIsDone = true;
        mError  = kListenFailure;
        return;
    }
    mPort   = mListener->getPort();
    mThread = std::thread { &FakeMudServer::run, this };
}

FakeMudServer::~FakeMudServer()
{
    {
        const std::scoped_lock lock { mMutex };
        mIsStopping = true;
    }
    if (mThread.joinable())
        mThread.join();
}

bool FakeMudServer::waitDone(std::chrono::milliseconds timeout)
{
    std::unique_lock lock { mMutex };
    const auto       isDone = [this]()
    {
        return mIsDone;
    };
    return mChanged.wait_for(lock, timeout, isDone);
}

std::string FakeMudServer::getError()
{
    const std::scoped_lock lock { mMutex };
    return mError;
}

types::Bytes FakeMudServer::getReceived()
{
    const std::scoped_lock lock { mMutex };
    return mReceived;
}

void FakeMudServer::fail(std::string error)
{
    {
        const std::scoped_lock lock { mMutex };
        if (mError.empty())
            mError = std::move(error);
        mIsDone = true;
    }
    mChanged.notify_all();
}

bool FakeMudServer::readAvailable(const transport::TcpConnection& connection)
{
    std::array<std::byte, kReadSize> buffer {};
    while (true)
    {
        const transport::IoResult result { connection.read(buffer) };
        if (result.mStatus == transport::IoStatus::kWouldBlock)
            return true;
        if (result.mStatus != transport::IoStatus::kDone)
            return false;
        const std::scoped_lock lock { mMutex };
        const auto             end { buffer.begin() + static_cast<std::ptrdiff_t>(result.mCount) };
        mReceived.insert(mReceived.end(), buffer.begin(), end);
    }
}

bool FakeMudServer::writeAll(const transport::TcpConnection& connection, std::span<const std::byte> bytes)
{
    std::size_t written { 0 };
    while (written < bytes.size())
    {
        const transport::IoResult result { connection.write(bytes.subspan(written)) };
        if (result.mStatus == transport::IoStatus::kWouldBlock)
        {
            static_cast<void>(transport::pollSockets(connection, true, nullptr, kPollSlice));
            continue;
        }
        if (result.mStatus != transport::IoStatus::kDone)
            return false;
        written += result.mCount;
    }
    return true;
}

bool FakeMudServer::awaitExpected(const transport::TcpConnection& connection, const types::Bytes& bytes)
{
    const auto deadline { std::chrono::steady_clock::now() + kExpectTimeout };
    while (true)
    {
        const bool isOpen { readAvailable(connection) };
        {
            const std::scoped_lock lock { mMutex };
            if (mReceived.size() >= mCursor + bytes.size())
            {
                const std::span<const std::byte> received { mReceived };
                const std::span<const std::byte> got { received.subspan(mCursor, bytes.size()) };
                if (! std::ranges::equal(got, bytes))
                {
                    mError = "expected [" + describe(bytes) + "] but the client sent [" + describe(got) + "]";
                    return false;
                }
                mCursor += bytes.size();
                return true;
            }
            if (mIsStopping)
                return false;
        }
        if (! isOpen)
        {
            fail("the client closed the connection while [" + describe(bytes) + "] was expected");
            return false;
        }
        if (std::chrono::steady_clock::now() > deadline)
        {
            fail("timed out waiting for [" + describe(bytes) + "]");
            return false;
        }
        static_cast<void>(transport::pollSockets(connection, false, nullptr, kPollSlice));
    }
}

bool FakeMudServer::awaitCount(const transport::TcpConnection& connection, std::size_t count)
{
    const auto deadline { std::chrono::steady_clock::now() + kExpectTimeout };
    while (true)
    {
        const bool isOpen { readAvailable(connection) };
        {
            const std::scoped_lock lock { mMutex };
            if (mReceived.size() >= mCursor + count)
            {
                mCursor += count;
                return true;
            }
            if (mIsStopping)
                return false;
        }
        if (! isOpen)
        {
            fail("the client closed the connection while " + std::to_string(count) + " bytes were expected");
            return false;
        }
        if (std::chrono::steady_clock::now() > deadline)
        {
            fail("timed out waiting for " + std::to_string(count) + " bytes");
            return false;
        }
        static_cast<void>(transport::pollSockets(connection, false, nullptr, kPollSlice));
    }
}

void FakeMudServer::run()
{
    std::optional<transport::TcpConnection> connection;
    if (mListener.has_value())
        connection = mListener->accept(kAcceptTimeout);
    if (! connection.has_value())
    {
        fail("no client connected");
        return;
    }

    for (const ScriptStep& step : mScript)
    {
        bool isOk { true };
        switch (step.mKind)
        {
            case ScriptStep::Kind::kSend:
                isOk = writeAll(*connection, step.mBytes);
                if (! isOk)
                    fail("a send failed");
                break;
            case ScriptStep::Kind::kPause:
                std::this_thread::sleep_for(step.mPause);
                break;
            case ScriptStep::Kind::kExpect:
                isOk = awaitExpected(*connection, step.mBytes);
                if (! isOk)
                    fail({});
                break;
            case ScriptStep::Kind::kExpectCount:
                isOk = awaitCount(*connection, step.mCount);
                if (! isOk)
                    fail({});
                break;
        }
        if (! isOk)
            return;
    }

    {
        const std::scoped_lock lock { mMutex };
        mIsDone = true;
    }
    mChanged.notify_all();

    // Keep reading what the client sends until the test is finished with the server.
    while (true)
    {
        {
            const std::scoped_lock lock { mMutex };
            if (mIsStopping)
                return;
        }
        if (! readAvailable(*connection))
            return;
        static_cast<void>(transport::pollSockets(*connection, false, nullptr, kPollSlice));
    }
}
} // namespace muckaii::core::fake
