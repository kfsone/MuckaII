#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/transport/loopback/tcpListener.hpp"
#include "muckaii/transport/socket.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Test support (docs/build.md "Targets": the loopback fake server). A scripted MUD2 stand-in on
// 127.0.0.1 and an ephemeral port: it accepts one client, plays its script, and records every byte
// the client sends. It never touches a real server.
namespace muckaii::core::fake
{
// ScriptStep describes one send, expectation, or pause in a fake server transcript.
struct ScriptStep
{
    // Kind selects the action the scripted fake server performs.
    enum class Kind : std::uint8_t
    {
        kSend,        // write these bytes as one write
        kExpect,      // wait until the client has sent exactly these bytes next
        kExpectCount, // wait until the client has sent mCount more bytes, whatever they are
        kPause,       // sleep, so the next send lands as a read of its own
    };

    types::Bytes              mBytes;
    std::chrono::milliseconds mPause {};
    std::size_t               mCount {};
    Kind                      mKind { Kind::kSend };
};

// send creates a step that sends the text bytes.
[[nodiscard]] ScriptStep send(std::string_view text);
// send creates a step that sends the supplied bytes.
[[nodiscard]] ScriptStep send(types::Bytes bytes);
// expect creates a step that waits for the supplied text bytes from the client.
[[nodiscard]] ScriptStep expect(std::string_view text);
// expect creates a step that waits for the supplied bytes from the client.
[[nodiscard]] ScriptStep expect(types::Bytes bytes);
// expectCount waits for count additional client bytes without inspecting their values.
[[nodiscard]] ScriptStep expectCount(std::size_t count);
// pause creates a step that delays the next scripted action.
[[nodiscard]] ScriptStep pause(std::chrono::milliseconds duration);

using CapturePath   = std::filesystem::path;
using CaptureGap    = std::chrono::milliseconds;
using CaptureScript = std::vector<ScriptStep>;

// replayCapture sends each received capture record separately and returns no steps on read failure.
[[nodiscard]] CaptureScript replayCapture(const CapturePath& capture, CaptureGap gap);

// getExpectedBytes concatenates the client bytes required by all expectation steps.
[[nodiscard]] types::Bytes getExpectedBytes(const std::vector<ScriptStep>& script);

// FakeMudServer runs a scripted loopback peer and records the client's transmitted bytes.
class FakeMudServer
{
public:
    // FakeMudServer starts a listener and executes script on its worker thread.
    explicit FakeMudServer(std::vector<ScriptStep> script);
    // ~FakeMudServer stops the script worker and joins it.
    ~FakeMudServer();
    FakeMudServer(const FakeMudServer&)            = delete;
    FakeMudServer& operator=(const FakeMudServer&) = delete;
    FakeMudServer(FakeMudServer&&)                 = delete;
    FakeMudServer& operator=(FakeMudServer&&)      = delete;

    // getPort returns the ephemeral port, or zero if the listener could not open.
    [[nodiscard]] std::uint16_t getPort() const noexcept
    {
        return mPort;
    }

    // waitDone waits for script completion and returns false on timeout.
    [[nodiscard]] bool waitDone(std::chrono::milliseconds timeout);

    // getError returns the first script failure, or an empty string on success.
    [[nodiscard]] std::string getError();

    // getReceived returns all client bytes collected so far.
    [[nodiscard]] types::Bytes getReceived();

private:
    // run accepts one client and executes the scripted transcript.
    void run();
    // fail stores the first script error and wakes waiters.
    void fail(std::string error);
    // readAvailable collects bytes until the connection would block.
    [[nodiscard]] bool readAvailable(const transport::TcpConnection& connection);
    // writeAll sends every byte unless the connection fails.
    [[nodiscard]] bool writeAll(const transport::TcpConnection& connection, std::span<const std::byte> bytes);
    // awaitCount waits until count additional bytes arrive from the client.
    [[nodiscard]] bool awaitCount(const transport::TcpConnection& connection, std::size_t count);
    // awaitExpected checks the next client bytes against bytes.
    [[nodiscard]] bool awaitExpected(const transport::TcpConnection& connection, const types::Bytes& bytes);

    transport::Network                              mNetwork;
    std::optional<transport::loopback::TcpListener> mListener;
    std::size_t                                     mCursor {};
    std::vector<ScriptStep>                         mScript;
    std::mutex                                      mMutex;
    std::condition_variable                         mChanged;
    types::Bytes                                    mReceived;
    std::string                                     mError;
    std::thread                                     mThread;
    std::uint16_t                                   mPort {};
    bool                                            mIsDone {};
    bool                                            mIsStopping {};
};
} // namespace muckaii::core::fake
