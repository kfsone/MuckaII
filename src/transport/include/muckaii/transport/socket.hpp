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

// kInvalidSocket marks a socket handle that owns no socket.
constexpr SocketHandle kInvalidSocket { ~SocketHandle { 0 } };

// The connect timeout (docs/spec/connection.md 1): overall, resolution included.
// kConnectTimeout is the overall default connection deadline.
constexpr std::chrono::milliseconds kConnectTimeout { 15000 };

// Starts the platform's socket library for the life of this object (Winsock; nothing on POSIX).
// Network starts and stops the platform socket library.
class Network
{
public:
    // Network initializes the platform socket library when needed.
    Network() noexcept;
    // ~Network shuts down the platform socket library when initialization succeeded.
    ~Network();
    // Network cannot be copied because it owns platform initialization.
    Network(const Network&) = delete;
    // operator= rejects copying platform initialization ownership.
    Network& operator=(const Network&) = delete;
    // Network cannot be moved because it owns platform initialization.
    Network(Network&&) = delete;
    // operator= rejects moving platform initialization ownership.
    Network& operator=(Network&&) = delete;

    // isReady reports whether platform socket initialization succeeded.
    [[nodiscard]] bool isReady() const noexcept
    {
        return mIsReady;
    }

private:
    bool mIsReady {};
};

// IoStatus identifies the outcome of one non-blocking socket operation.
enum class IoStatus : std::uint8_t
{
    kDone,
    kWouldBlock,
    kClosed,
    kError,
};

// IoResult reports the bytes transferred and the socket operation outcome.
struct IoResult
{
    std::size_t mCount {};
    IoStatus    mStatus { IoStatus::kDone };
};

// A connected, non-blocking TCP socket with Nagle off. Move-only; closes on destruction.
// TcpConnection owns one non-blocking TCP socket and closes it on destruction.
class TcpConnection
{
public:
    // TcpConnection creates an empty connection.
    TcpConnection() noexcept = default;
    // TcpConnection takes ownership of an existing socket handle.
    explicit TcpConnection(SocketHandle handle) noexcept;
    // ~TcpConnection closes the owned socket.
    ~TcpConnection();
    // TcpConnection cannot be copied because it uniquely owns its socket.
    TcpConnection(const TcpConnection&) = delete;
    // operator= rejects copying unique socket ownership.
    TcpConnection& operator=(const TcpConnection&) = delete;
    // TcpConnection transfers socket ownership from another connection.
    TcpConnection(TcpConnection&& other) noexcept;
    // operator= closes the current socket and transfers ownership.
    TcpConnection& operator=(TcpConnection&& other) noexcept;

    // Reads what is available. kClosed when the peer closed (a read of 0 bytes).
    // read receives available bytes without blocking.
    [[nodiscard]] IoResult read(std::span<std::byte> buffer) const noexcept;

    // Writes what the socket takes now; mCount may be less than the span.
    // write sends available bytes without blocking.
    [[nodiscard]] IoResult write(std::span<const std::byte> bytes) const noexcept;

    // close releases the socket and leaves this connection empty.
    void close() noexcept;

    // isOpen reports whether this connection owns a valid socket.
    [[nodiscard]] bool isOpen() const noexcept
    {
        return mHandle != kInvalidSocket;
    }

    // getHandle returns the underlying platform handle.
    [[nodiscard]] SocketHandle getHandle() const noexcept
    {
        return mHandle;
    }

private:
    SocketHandle mHandle { kInvalidSocket };
};

// A loopback UDP socket connected to itself. Any thread wakes the reactor by sending one byte to it
// (docs/spec/architecture.md "Threads").
// WakeSocket provides a thread-safe loopback signal for reactor polling.
class WakeSocket
{
public:
    // WakeSocket creates an empty wake channel.
    WakeSocket() noexcept = default;
    // ~WakeSocket closes the owned wake socket.
    ~WakeSocket();
    // WakeSocket cannot be copied because it uniquely owns its socket.
    WakeSocket(const WakeSocket&) = delete;
    // operator= rejects copying unique wake-socket ownership.
    WakeSocket& operator=(const WakeSocket&) = delete;
    // WakeSocket transfers socket ownership from another wake channel.
    WakeSocket(WakeSocket&& other) noexcept;
    WakeSocket& operator=(WakeSocket&& other) noexcept;

    // open creates a non-blocking loopback wake channel or returns no value on failure.
    [[nodiscard]] static std::optional<WakeSocket> open();

    // Safe from any thread.
    // wake sends a signal byte to interrupt a poll from any thread.
    void wake() const noexcept;

    // Reads and discards every pending wake byte.
    // drain discards all pending signal bytes.
    void drain() const noexcept;

    // getHandle returns the underlying platform handle.
    [[nodiscard]] SocketHandle getHandle() const noexcept
    {
        return mHandle;
    }

private:
    SocketHandle mHandle { kInvalidSocket };
};

// ConnectResult carries a successful connection or its failure or cancellation state.
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
// connectTcp resolves and connects to a host within one overall timeout.
[[nodiscard]]
auto connectTcp(const std::string&                       host,
                std::uint16_t                            port,
                std::chrono::milliseconds                timeout,
                const std::shared_ptr<const WakeSocket>& wake,
                const std::atomic<bool>&                 isCancelled) -> ConnectResult;

using Milliseconds = std::chrono::milliseconds;
// One address's share of the connect budget (connection.md 1): what is left, split evenly over
// the addresses not yet tried, this one included. The last address gets all that is left.
// getAttemptBudget divides remaining time among the untried addresses.
[[nodiscard]] auto getAttemptBudget(Milliseconds, std::size_t) noexcept -> Milliseconds;

// PollResult reports readiness and failure states from a socket poll.
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
// pollSockets waits for connection readiness or a wake signal.
[[nodiscard]] auto pollSockets(const TcpConnection&, bool, const WakeSocket*, Milliseconds) noexcept
    -> PollResult;

// Sets a socket made outside this library (test support) non-blocking, with Nagle off.
// makeNonBlocking configures an external socket for non-blocking TCP use.
[[nodiscard]] bool makeNonBlocking(SocketHandle handle) noexcept;
} // namespace muckaii::transport
