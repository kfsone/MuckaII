#pragma once

#include "muckaii/transport/socket.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

// Test support (docs/build.md "Targets": muckaiiTransportLoopback): a listening socket on 127.0.0.1
// and an ephemeral port, for the loopback fake servers. Never part of a shipped binary.
namespace muckaii::transport::loopback
{
// TcpListener owns a loopback TCP listener used by transport tests.
class TcpListener
{
public:
    // TcpListener creates an empty listener.
    TcpListener() noexcept = default;
    // ~TcpListener closes the listening socket.
    ~TcpListener();
    // TcpListener cannot be copied because it uniquely owns its socket.
    TcpListener(const TcpListener&) = delete;
    // operator= rejects copying unique listener ownership.
    TcpListener& operator=(const TcpListener&) = delete;
    // TcpListener transfers socket ownership from another listener.
    TcpListener(TcpListener&& other) noexcept;
    // operator= closes the current socket and transfers ownership.
    TcpListener& operator=(TcpListener&& other) noexcept;

    // Listens with a backlog of `backlog` pending connections.
    // listen creates a listener on an ephemeral loopback port.
    [[nodiscard]] static std::optional<TcpListener> listen(int backlog = 1);

    // Waits up to `timeout` for one connection, returned non-blocking with Nagle off.
    // accept waits for a client and returns a non-blocking connection.
    [[nodiscard]] std::optional<TcpConnection> accept(std::chrono::milliseconds timeout) const noexcept;

    // getPort returns the listener's ephemeral port.
    [[nodiscard]] std::uint16_t getPort() const noexcept
    {
        return mPort;
    }

private:
    // close releases the listening socket.
    void close() noexcept;

    SocketHandle  mHandle { kInvalidSocket };
    std::uint16_t mPort {};
};
} // namespace muckaii::transport::loopback
