#pragma once

#include "muckaii/transport/socket.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

// Test support (docs/build.md "Targets": muckaiiTransportLoopback): a listening socket on 127.0.0.1
// and an ephemeral port, for the loopback fake servers. Never part of a shipped binary.
namespace muckaii::transport::loopback
{
class TcpListener
{
public:
    TcpListener() noexcept = default;
    ~TcpListener();
    TcpListener(const TcpListener&)            = delete;
    TcpListener& operator=(const TcpListener&) = delete;
    TcpListener(TcpListener&& other) noexcept;
    TcpListener& operator=(TcpListener&& other) noexcept;

    // Listens with a backlog of `backlog` pending connections.
    [[nodiscard]] static std::optional<TcpListener> listen(int backlog = 1);

    // Waits up to `timeout` for one connection, returned non-blocking with Nagle off.
    [[nodiscard]] std::optional<TcpConnection> accept(std::chrono::milliseconds timeout) const noexcept;

    [[nodiscard]] std::uint16_t getPort() const noexcept
    {
        return mPort;
    }

private:
    void close() noexcept;

    SocketHandle  mHandle { kInvalidSocket };
    std::uint16_t mPort {};
};
} // namespace muckaii::transport::loopback
