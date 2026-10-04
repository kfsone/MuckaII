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

// The core thread (docs/spec/architecture.md "Threads"): one reactor that owns the socket, the
// session (and the decoder in it) and the wire-log handover. Frontends talk to it only through
// submitLine() and setColumns(), which push onto the UI -> core FIFO and never block, and receive
// event batches through the sink, called on the core thread.
namespace muckaii::core
{
// Called on the core thread with one batch per socket read (and one per other change).
using EventSink = std::function<void(std::vector<session::SessionEvent>)>;

class Builder;

class Core
{
    // Only a Builder makes a Core (architecture.md "Composition").
    class Key
    {
        friend class Builder;
        Key() = default;
    };

    struct Config
    {
        std::string                          mHost;
        session::Credentials                 mCredentials;
        std::optional<std::filesystem::path> mWireLogPath;
        std::string                          mClientVersion;
        EventSink                            mSink;
        std::chrono::milliseconds            mConnectTimeout { std::chrono::seconds { 15 } };
        int                                  mColumns { session::kDefaultColumns };
        std::uint16_t                        mPort { 23 };
        bool                                 mIsDefaultWireLog {};
    };

    friend class Builder;

public:
    Core(Key key, Config config);
    ~Core();
    Core(const Core&)            = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&)                 = delete;
    Core& operator=(Core&&)      = delete;

    // Starts the core thread, which connects at once (direct connect). A Core is single use: one
    // connection, then stop(). False if the reactor's wake socket could not be opened, and on any
    // second call.
    [[nodiscard]] bool start();

    // From the UI thread: queues one typed line in arrival order (Invariant #1). Never blocks
    // beyond a push under the FIFO's lock. Returns core's verdict on whether the line is sensitive
    // (session::isInputSensitive): the frontend keeps a sensitive line out of its recall list, and
    // the wire log masks it whatever the flow is when it is sent. Core is the one source of it.
    bool submitLine(std::string text);

    // From the UI thread: whether a line typed now is sensitive, for hiding the command box.
    [[nodiscard]] bool isInputSensitive() const noexcept
    {
        return mIsInputSensitive.load();
    }

    // From the UI thread: the effective column count changed.
    void setColumns(int columns);

    // Stops promptly, even mid-connect. Lines already submitted are written first, within a
    // bounded wait. Joins the core thread. Idempotent.
    void stop();

private:
    struct SubmitCommand
    {
        std::string mText;
        bool        mIsSensitive {};
    };
    struct ColumnsCommand
    {
        int mColumns {};
    };
    using Command = std::variant<SubmitCommand, ColumnsCommand>;

    class Reactor;

    void push(Command command);
    void run();

    Config                                       mConfig;
    std::mutex                                   mMutex;
    std::vector<Command>                         mFifo;
    std::unique_ptr<transport::Network>          mNetwork;
    std::shared_ptr<const transport::WakeSocket> mWake;
    std::thread                                  mThread;
    std::atomic<bool>                            mIsStopping; // value-initialised to false (C++20)
    std::atomic<bool>                            mIsInputSensitive { true }; // no game yet
    bool                                         mHasStarted {};
};

// Assembles the stack a consumer needs (architecture.md "Composition"): transport, mud and the
// session's login always; the wire log only when asked for.
class Builder
{
public:
    Builder& setHost(std::string host);
    Builder& setPort(std::uint16_t port);
    Builder& setCredentials(const session::Credentials& credentials);
    Builder& setColumns(int columns);
    Builder& setClientVersion(std::string version);
    Builder& setConnectTimeout(std::chrono::milliseconds timeout);
    Builder& setSink(EventSink sink);

    // The wire log at `path`.
    Builder& setWireLog(std::filesystem::path path);

    // The wire log at ~/.muckaii/muckaii.db (persistence.md "Data directory"). Without a home
    // directory there is none, and the run says so in a notice.
    Builder& setDefaultWireLog();

    [[nodiscard]] std::unique_ptr<Core> build();

private:
    Core::Config mConfig;
};
} // namespace muckaii::core
