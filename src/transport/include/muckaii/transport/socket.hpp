#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

// Plain TCP (docs/spec/connection.md 1) and the reactor's wake socket. Platform socket headers stay
// in socket.cpp: a SOCKET (Windows) or descriptor (POSIX) is carried here as an integer.
namespace muckaii::transport
{
using SocketHandle = std::uintptr_t;

constexpr SocketHandle kInvalidSocket { ~SocketHandle { 0 } };

// The connect timeout (docs/spec/connection.md 1): overall, resolution included.
constexpr std::chrono::milliseconds kConnectTimeout { 15000 };

// Starts the platform's socket library for the life of this object (Winsock; nothing on POSIX).
class Network
{
public:
    Network() noexcept;
    ~Network();
    Network(const Network&)            = delete;
    Network& operator=(const Network&) = delete;
    Network(Network&&)                 = delete;
    Network& operator=(Network&&)      = delete;

    [[nodiscard]] bool isReady() const noexcept
    {
        return mIsReady;
    }

private:
    bool mIsReady {};
};

enum class IoStatus : std::uint8_t
{
    kDone,
    kWouldBlock,
    kClosed,
    kError,
};

struct IoResult
{
    std::size_t mCount {};
    IoStatus    mStatus { IoStatus::kDone };
};

// A connected, non-blocking TCP socket with Nagle off. Move-only; closes on destruction.
class TcpConnection
{
public:
    TcpConnection() noexcept = default;
    explicit TcpConnection(SocketHandle handle) noexcept;
    ~TcpConnection();
    TcpConnection(const TcpConnection&)            = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&& other) noexcept;
    TcpConnection& operator=(TcpConnection&& other) noexcept;

    // Reads what is available. kClosed when the peer closed (a read of 0 bytes).
    [[nodiscard]] IoResult read(std::span<std::byte> buffer) const noexcept;

    // Writes what the socket takes now; mCount may be less than the span.
    [[nodiscard]] IoResult write(std::span<const std::byte> bytes) const noexcept;

    void close() noexcept;

    [[nodiscard]] bool isOpen() const noexcept
    {
        return mHandle != kInvalidSocket;
    }

    [[nodiscard]] SocketHandle getHandle() const noexcept
    {
        return mHandle;
    }

private:
    SocketHandle mHandle { kInvalidSocket };
};

// A loopback UDP socket connected to itself. Any thread wakes the reactor by sending one byte to it
// (docs/spec/architecture.md "Threads").
class WakeSocket
{
public:
    WakeSocket() noexcept = default;
    ~WakeSocket();
    WakeSocket(const WakeSocket&)            = delete;
    WakeSocket& operator=(const WakeSocket&) = delete;
    WakeSocket(WakeSocket&& other) noexcept;
    WakeSocket& operator=(WakeSocket&& other) noexcept;

    [[nodiscard]] static std::optional<WakeSocket> open();

    // Safe from any thread.
    void wake() const noexcept;

    // Reads and discards every pending wake byte.
    void drain() const noexcept;

    [[nodiscard]] SocketHandle getHandle() const noexcept
    {
        return mHandle;
    }

private:
    SocketHandle mHandle { kInvalidSocket };
};

struct ConnectResult
{
    std::optional<TcpConnection> mConnection;
    std::string                  mError;          // why it failed; empty when connected or cancelled
    bool                         mIsCancelled {}; // `isCancelled` was set while connecting
};

// Resolves `host` and connects, within `timeout` overall, each address with its share of it
// (getAttemptBudget). Resolution is asynchronous: GetAddrInfoExW, cancelled on a cancel or the
// deadline (Windows); getaddrinfo on a short-lived thread that holds `wake` until it finishes
// (POSIX, where it cannot be cancelled). The connect is non-blocking and polled together with
// `wake`: every wake re-checks `isCancelled`, so a cancel returns at once. Wake bytes are drained
// here; whatever woke the caller is still waiting for it.
[[nodiscard]] ConnectResult connectTcp(const std::string&                       host,
                                       std::uint16_t                            port,
                                       std::chrono::milliseconds                timeout,
                                       const std::shared_ptr<const WakeSocket>& wake,
                                       const std::atomic<bool>&                 isCancelled);

// One address's share of the connect budget (connection.md 1): what is left, split evenly over
// the addresses not yet tried, this one included. The last address gets all that is left.
[[nodiscard]] std::chrono::milliseconds getAttemptBudget(std::chrono::milliseconds left,
                                                         std::size_t               addressesLeft) noexcept;

struct PollResult
{
    bool mIsReadable {};
    bool mIsWritable {};
    bool mIsWoken {};
    bool mHasError {};
    bool mIsFailed {}; // poll() itself failed
};

// One poll() (WSAPoll() on Windows) over the connection, if open, and the wake socket, if given. A
// negative timeout waits for ever.
[[nodiscard]] PollResult pollSockets(const TcpConnection&      connection,
                                     bool                      willWrite,
                                     const WakeSocket*         wake,
                                     std::chrono::milliseconds timeout) noexcept;

// Sets a socket made outside this library (test support) non-blocking, with Nagle off.
[[nodiscard]] bool makeNonBlocking(SocketHandle handle) noexcept;
} // namespace muckaii::transport
