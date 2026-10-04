#include "muckaii/transport/loopback/tcpListener.hpp"

#include <algorithm>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace muckaii::transport::loopback
{
namespace
{
#ifdef _WIN32
using NativeSocket = SOCKET;
using PollFd       = WSAPOLLFD;

void closeNative(NativeSocket socket) noexcept
{
    closesocket(socket);
}

int pollNative(PollFd* fd, int timeoutMs) noexcept
{
    return WSAPoll(fd, 1, timeoutMs);
}

bool isValid(NativeSocket socket) noexcept
{
    constexpr NativeSocket kInvalidNative { ~NativeSocket { 0 } };
    return socket != kInvalidNative;
}
#else
using NativeSocket = int;
using PollFd       = pollfd;

void closeNative(NativeSocket socket) noexcept
{
    ::close(socket);
}

int pollNative(PollFd* fd, int timeoutMs) noexcept
{
    return ::poll(fd, 1, timeoutMs);
}

bool isValid(NativeSocket socket) noexcept
{
    return socket >= 0;
}
#endif

NativeSocket toNative(SocketHandle handle) noexcept
{
    return static_cast<NativeSocket>(handle);
}

int toTimeoutMs(std::chrono::milliseconds timeout) noexcept
{
    constexpr long long kMaxTimeout { 0x7FFFFFFF };
    return static_cast<int>(std::clamp(timeout.count(), 0LL, kMaxTimeout));
}
} // namespace

TcpListener::~TcpListener()
{
    close();
}

TcpListener::TcpListener(TcpListener&& other) noexcept
    : mHandle { std::exchange(other.mHandle, kInvalidSocket) }
    , mPort { other.mPort }
{
}

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept
{
    if (this != &other)
    {
        close();
        mHandle = std::exchange(other.mHandle, kInvalidSocket);
        mPort   = other.mPort;
    }
    return *this;
}

void TcpListener::close() noexcept
{
    if (mHandle == kInvalidSocket)
        return;
    closeNative(toNative(mHandle));
    mHandle = kInvalidSocket;
}

std::optional<TcpListener> TcpListener::listen(int backlog)
{
    const NativeSocket socket { ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) };
    if (! isValid(socket))
        return std::nullopt;
    TcpListener listener;
    listener.mHandle = static_cast<SocketHandle>(socket);

    sockaddr_in address {};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = 0;
    socklen_t length { sizeof(address) };
    if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0
        || ::listen(socket, backlog) != 0
        || getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) != 0)
        return std::nullopt;
    listener.mPort = ntohs(address.sin_port);
    return listener;
}

std::optional<TcpConnection> TcpListener::accept(std::chrono::milliseconds timeout) const noexcept
{
    PollFd fd {};
    fd.fd     = toNative(mHandle);
    fd.events = POLLIN;
    if (pollNative(&fd, toTimeoutMs(timeout)) <= 0)
        return std::nullopt;
    const NativeSocket socket { ::accept(toNative(mHandle), nullptr, nullptr) };
    if (! isValid(socket))
        return std::nullopt;
    TcpConnection connection { static_cast<SocketHandle>(socket) };
    if (! makeNonBlocking(connection.getHandle()))
        return std::nullopt;
    return connection;
}
} // namespace muckaii::transport::loopback
