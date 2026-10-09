#pragma once

#include "muckaii/session/session.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace muckaii::transport
{
class Network;
class WakeSocket;
} // namespace muckaii::transport

// Core owns one network session and receives frontend commands through an ordered FIFO.
namespace muckaii::core
{
// Called on the core thread with one batch per socket read (and one per other change).
using EventSink = std::function<void(std::vector<session::SessionEvent>)>;

// kDefaultConnectTimeout bounds the initial server connection attempt.
constexpr std::chrono::milliseconds kDefaultConnectTimeout { std::chrono::seconds { 15 } };
// kDefaultPort is the standard MUD2 telnet port.
constexpr std::uint16_t kDefaultPort { 23 };

class Builder;

class Core
{
    // Only a Builder makes a Core (architecture.md "Composition").
    // Key prevents consumers from constructing Core outside Builder.
    class Key
    {
        friend class Builder;
        // Key prevents direct construction by Core consumers.
        Key() = default;
    };

    // Config holds the connection options transferred from Builder to Core.
    struct Config
    {
        std::string                          mHost;
        session::Credentials                 mCredentials;
        std::optional<std::filesystem::path> mWireLogPath;
        std::string                          mClientVersion;
        EventSink                            mSink;
        std::chrono::milliseconds            mConnectTimeout { kDefaultConnectTimeout };
        int                                  mColumns { session::kDefaultColumns };
        std::uint16_t                        mPort { kDefaultPort };
        bool                                 mIsDefaultWireLog {};
        bool                                 mIsClientWrap {}; // connection.md 4.2: Terminal Width 0
    };

    friend class Builder;

public:
    // Core takes the connection configuration assembled by Builder.
    Core(Key key, Config config);
    // ~Core stops and joins the reactor before its owned state is destroyed.
    ~Core();
    Core(const Core&)            = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&)                 = delete;
    Core& operator=(Core&&)      = delete;

    // start launches the single-use connection and fails if its wake socket cannot be opened.
    [[nodiscard]] bool start();

    // submitLine queues the exact typed line and returns whether it must stay out of recall.
    bool submitLine(std::string text);

    // setColumns queues the new effective terminal width for the session.
    void setColumns(int columns);

    // stop wakes and joins the reactor after its bounded flush of submitted lines.
    void stop();

private:
    // SubmitCommand preserves a typed line and the sensitivity verdict from its arrival.
    struct SubmitCommand
    {
        std::string mText;
        bool        mIsSensitive {};
    };
    // ColumnsCommand carries a frontend column-count change to the reactor.
    struct ColumnsCommand
    {
        int mColumns {};
    };
    using Command = std::variant<SubmitCommand, ColumnsCommand>;

    class Reactor;

    // push appends a frontend command and wakes the reactor.
    void push(Command command);
    // run owns the reactor for the lifetime of the core thread.
    void run();

    Config                                       mConfig;
    std::mutex                                   mMutex;
    std::vector<Command>                         mFifo;
    std::unique_ptr<transport::Network>          mNetwork;
    std::shared_ptr<const transport::WakeSocket> mWake;
    std::thread                                  mThread;
    std::atomic<bool>                            mIsStopping; // value-initialised to false (C++20)
    std::atomic<bool>                            mIsInputSensitive { true }; // not logged in yet
    bool                                         mHasStarted {};
};

// Builder assembles the connection settings and creates a Core.
class Builder
{
public:
    // setHost selects the server hostname or address.
    Builder& setHost(std::string host);
    // setPort selects the server TCP port.
    Builder& setPort(std::uint16_t port);
    // setCredentials supplies the account, password, and login name for the session.
    Builder& setCredentials(const session::Credentials& credentials);
    // setColumns sets the initial effective terminal width.
    Builder& setColumns(int columns);
    // setClientWrap enables client-side wrapping for Terminal Width 0.
    Builder& setClientWrap(bool isClientWrap);
    // setClientVersion records the client version in each wire-log run.
    Builder& setClientVersion(std::string version);
    // setConnectTimeout bounds the connection attempt.
    Builder& setConnectTimeout(std::chrono::milliseconds timeout);
    // setSink receives event batches on the core thread.
    Builder& setSink(EventSink sink);

    // setWireLog stores this run in the database at path.
    Builder& setWireLog(std::filesystem::path path);

    // setDefaultWireLog selects the standard user data database when a home directory exists.
    Builder& setDefaultWireLog();

    // build transfers the configured connection settings into a new Core.
    [[nodiscard]] std::unique_ptr<Core> build();

private:
    Core::Config mConfig;
};
} // namespace muckaii::core
