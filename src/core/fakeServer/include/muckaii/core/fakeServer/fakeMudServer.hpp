#pragma once

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
struct ScriptStep
{
    enum class Kind : std::uint8_t
    {
        kSend,        // write these bytes as one write
        kExpect,      // wait until the client has sent exactly these bytes next
        kExpectCount, // wait until the client has sent mCount more bytes, whatever they are
        kPause,       // sleep, so the next send lands as a read of its own
    };

    std::vector<std::byte>    mBytes;
    std::chrono::milliseconds mPause {};
    std::size_t               mCount {};
    Kind                      mKind { Kind::kSend };
};

[[nodiscard]] ScriptStep send(std::string_view text);
[[nodiscard]] ScriptStep send(std::vector<std::byte> bytes);
[[nodiscard]] ScriptStep expect(std::string_view text);
[[nodiscard]] ScriptStep expect(std::vector<std::byte> bytes);
[[nodiscard]] ScriptStep expectCount(std::size_t count);
[[nodiscard]] ScriptStep pause(std::chrono::milliseconds duration);

// Every rx record of a `.c1` capture as its own send, each followed by `gap`. Empty if the capture
// cannot be read.
[[nodiscard]] std::vector<ScriptStep> replayCapture(const std::filesystem::path& capture,
                                                    std::chrono::milliseconds    gap);

// The bytes every kExpect step names, concatenated: what the client must have sent, in order.
[[nodiscard]] std::vector<std::byte> getExpectedBytes(const std::vector<ScriptStep>& script);

class FakeMudServer
{
public:
    explicit FakeMudServer(std::vector<ScriptStep> script);
    ~FakeMudServer();
    FakeMudServer(const FakeMudServer&)            = delete;
    FakeMudServer& operator=(const FakeMudServer&) = delete;
    FakeMudServer(FakeMudServer&&)                 = delete;
    FakeMudServer& operator=(FakeMudServer&&)      = delete;

    // 0 if the server could not listen.
    [[nodiscard]] std::uint16_t getPort() const noexcept
    {
        return mPort;
    }

    // Waits for the script to finish or fail. False on timeout.
    [[nodiscard]] bool waitDone(std::chrono::milliseconds timeout);

    // Empty when every step succeeded.
    [[nodiscard]] std::string getError();

    // Every byte the client has sent so far.
    [[nodiscard]] std::vector<std::byte> getReceived();

private:
    void run();
    void fail(std::string error);
    [[nodiscard]] bool readAvailable(const transport::TcpConnection& connection);
    [[nodiscard]] bool writeAll(const transport::TcpConnection& connection, std::span<const std::byte> bytes);
    [[nodiscard]] bool awaitCount(const transport::TcpConnection& connection, std::size_t count);
    [[nodiscard]] bool awaitExpected(const transport::TcpConnection& connection,
                                     const std::vector<std::byte>&   bytes);

    transport::Network                              mNetwork;
    std::optional<transport::loopback::TcpListener> mListener;
    std::size_t                                     mCursor {};
    std::vector<ScriptStep>                         mScript;
    std::mutex                                      mMutex;
    std::condition_variable                         mChanged;
    std::vector<std::byte>                          mReceived;
    std::string                                     mError;
    std::thread                                     mThread;
    std::uint16_t                                   mPort {};
    bool                                            mIsDone {};
    bool                                            mIsStopping {};
};
} // namespace muckaii::core::fake
