#include "muckaii/transport/socket.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

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
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace muckaii::transport
{
namespace
{
#ifdef _WIN32
using NativeSocket = SOCKET;
using PollFd       = WSAPOLLFD;
constexpr int kWouldBlock { WSAEWOULDBLOCK };
constexpr int kInProgress { WSAEWOULDBLOCK };
constexpr int kTimedOut { WSAETIMEDOUT };

int getLastError() noexcept
{
    return WSAGetLastError();
}

void closeNative(NativeSocket socket) noexcept
{
    closesocket(socket);
}

bool setNonBlocking(NativeSocket socket) noexcept
{
    u_long isNonBlocking { 1 };
    return ioctlsocket(socket, FIONBIO, &isNonBlocking) == 0;
}

int pollNative(PollFd* fds, std::size_t count, int timeoutMs) noexcept
{
    return WSAPoll(fds, static_cast<ULONG>(count), timeoutMs);
}

int sendNative(NativeSocket socket, const std::byte* data, std::size_t size) noexcept
{
    return send(socket, reinterpret_cast<const char*>(data), static_cast<int>(size), 0);
}

int recvNative(NativeSocket socket, std::byte* data, std::size_t size) noexcept
{
    return recv(socket, reinterpret_cast<char*>(data), static_cast<int>(size), 0);
}
#else
using NativeSocket = int;
using PollFd       = pollfd;
constexpr int kWouldBlock { EWOULDBLOCK };
constexpr int kInProgress { EINPROGRESS };
constexpr int kTimedOut { ETIMEDOUT };

int getLastError() noexcept
{
    return errno;
}

void closeNative(NativeSocket socket) noexcept
{
    ::close(socket);
}

bool setNonBlocking(NativeSocket socket) noexcept
{
    const int flags { fcntl(socket, F_GETFL, 0) };
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}

int pollNative(PollFd* fds, std::size_t count, int timeoutMs) noexcept
{
    return ::poll(fds, static_cast<nfds_t>(count), timeoutMs);
}

int sendNative(NativeSocket socket, const std::byte* data, std::size_t size) noexcept
{
    return static_cast<int>(::send(socket, data, size, MSG_NOSIGNAL));
}

int recvNative(NativeSocket socket, std::byte* data, std::size_t size) noexcept
{
    return static_cast<int>(::recv(socket, data, size, 0));
}
#endif

NativeSocket toNative(SocketHandle handle) noexcept
{
    return static_cast<NativeSocket>(handle);
}

SocketHandle toHandle(NativeSocket socket) noexcept
{
    return static_cast<SocketHandle>(socket);
}

bool isValid(NativeSocket socket) noexcept
{
#ifdef _WIN32
    // INVALID_SOCKET, without the macro's C-style cast.
    constexpr NativeSocket kInvalidNative { ~NativeSocket { 0 } };
    return socket != kInvalidNative;
#else
    return socket >= 0;
#endif
}

void setNoDelay(NativeSocket socket) noexcept
{
    int isNoDelay { 1 };
    setsockopt(
        socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&isNoDelay), sizeof(isNoDelay));
}

int toTimeoutMs(std::chrono::milliseconds timeout) noexcept
{
    constexpr long long kMaxTimeout { 0x7FFFFFFF };
    if (timeout.count() < 0)
        return -1;
    return static_cast<int>(std::min(timeout.count(), kMaxTimeout));
}

std::string describeError(int code)
{
    switch (code)
    {
#ifdef _WIN32
        case WSAECONNREFUSED:
            return "connection refused";
        case WSAETIMEDOUT:
            return "connection timed out";
        case WSAEHOSTUNREACH:
        case WSAENETUNREACH:
            return "host unreachable";
#else
        case ECONNREFUSED:
            return "connection refused";
        case ETIMEDOUT:
            return "connection timed out";
        case EHOSTUNREACH:
        case ENETUNREACH:
            return "host unreachable";
#endif
        default:
            return "socket error " + std::to_string(code);
    }
}

// The error a finished non-blocking connect left on its socket; 0 when it connected.
int getConnectError(NativeSocket socket) noexcept
{
    int       error {};
    socklen_t length { sizeof(error) };
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0)
        return getLastError();
    return error;
}

// One resolved address, copied out of the resolver's list.
struct Address
{
    sockaddr_storage mStorage {};
    socklen_t        mLength {};
    int              mFamily {};
    int              mType {};
    int              mProtocol {};
};

enum class AttemptStatus : std::uint8_t
{
    kConnected, // for a resolution: the addresses are in
    kFailed,
    kTimedOut,
    kCancelled,
};

std::chrono::milliseconds getRemaining(std::chrono::steady_clock::time_point deadline) noexcept
{
    const auto left { deadline - std::chrono::steady_clock::now() };
    return std::max(std::chrono::duration_cast<std::chrono::milliseconds>(left),
                    std::chrono::milliseconds { 0 });
}

template <typename Entry> void appendAddress(std::vector<Address>& addresses, const Entry& entry)
{
    if (entry.ai_addrlen > sizeof(sockaddr_storage))
        return;
    Address address;
    std::memcpy(&address.mStorage, entry.ai_addr, entry.ai_addrlen);
    address.mLength   = static_cast<socklen_t>(entry.ai_addrlen);
    address.mFamily   = entry.ai_family;
    address.mType     = entry.ai_socktype;
    address.mProtocol = entry.ai_protocol;
    addresses.push_back(address);
}

// Waits on the wake socket alone until it fires or `left` passes, draining it. False when poll()
// itself failed.
bool waitForWake(const WakeSocket& wake, std::chrono::milliseconds left) noexcept
{
    const TcpConnection none;
    const PollResult    ready { pollSockets(none, false, &wake, left) };
    if (ready.mIsWoken)
        wake.drain();
    return ! ready.mIsFailed;
}

#ifdef _WIN32
// Windows: GetAddrInfoExW, asynchronous and cancellable. Its completion routine runs on a system
// thread and wakes the reactor; a cancel is waited out, so nothing outlives the connect.
struct WindowsLookup
{
    OVERLAPPED        mOverlapped {}; // first, so the routine's OVERLAPPED* is this lookup's address
    HANDLE            mCancel {};
    HANDLE            mDone {};
    ADDRINFOEXW*      mResult {};
    const WakeSocket* mWake {};
    DWORD             mError {};
};
static_assert(std::is_standard_layout_v<WindowsLookup>);

void CALLBACK onLookupDone(DWORD error, DWORD /*bytes*/, LPWSAOVERLAPPED overlapped)
{
    auto* const lookup { reinterpret_cast<WindowsLookup*>(overlapped) };
    lookup->mError = error;
    // Wake first: after SetEvent the connect may return and free the lookup.
    lookup->mWake->wake();
    SetEvent(lookup->mDone);
}

std::wstring toWide(const std::string& utf8)
{
    if (utf8.empty())
        return {};
    const int    length { MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0) };
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
    return wide;
}

AttemptStatus resolve(const std::string&                       host,
                      std::uint16_t                            port,
                      std::chrono::steady_clock::time_point    deadline,
                      const std::shared_ptr<const WakeSocket>& wake,
                      const std::atomic<bool>&                 isCancelled,
                      std::vector<Address>&                    addresses,
                      std::string&                             error)
{
    WindowsLookup lookup;
    lookup.mDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    lookup.mWake = wake.get();
    if (lookup.mDone == nullptr)
    {
        error = "cannot resolve " + host;
        return AttemptStatus::kFailed;
    }
    ADDRINFOEXW hints {};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    const std::wstring wideHost { toWide(host) };
    const std::wstring widePort { toWide(std::to_string(port)) };
    const INT          started { GetAddrInfoExW(wideHost.c_str(),
                                                widePort.c_str(),
                                                NS_ALL,
                                                nullptr,
                                                &hints,
                                                &lookup.mResult,
                                                nullptr,
                                                &lookup.mOverlapped,
                                                onLookupDone,
                                                &lookup.mCancel) };
    // Not pending: finished (or failed) at once, and the routine will not run.
    if (started != WSA_IO_PENDING)
        onLookupDone(static_cast<DWORD>(started), 0, &lookup.mOverlapped);

    AttemptStatus status { AttemptStatus::kConnected };
    while (WaitForSingleObject(lookup.mDone, 0) != WAIT_OBJECT_0)
    {
        const std::chrono::milliseconds left { getRemaining(deadline) };
        if (isCancelled.load())
            status = AttemptStatus::kCancelled;
        else if (left.count() == 0)
            status = AttemptStatus::kTimedOut;
        else if (! waitForWake(*wake, left))
            status = AttemptStatus::kFailed;
        if (status == AttemptStatus::kConnected)
            continue;
        GetAddrInfoExCancel(&lookup.mCancel);
        WaitForSingleObject(lookup.mDone, INFINITE);
        break;
    }
    if (status == AttemptStatus::kConnected && lookup.mError != NO_ERROR)
        status = AttemptStatus::kFailed;
    for (const ADDRINFOEXW* entry { lookup.mResult }; status == AttemptStatus::kConnected && entry != nullptr;
         entry = entry->ai_next)
        appendAddress(addresses, *entry);
    if (lookup.mResult != nullptr)
        FreeAddrInfoExW(lookup.mResult);
    CloseHandle(lookup.mDone);
    if (status == AttemptStatus::kConnected && addresses.empty())
        status = AttemptStatus::kFailed;
    if (status == AttemptStatus::kFailed)
        error = "cannot resolve " + host;
    return status;
}
#else
// POSIX: getaddrinfo cannot be cancelled, and getaddrinfo_a is glibc only (not bionic), so it runs
// on a short-lived detached thread. The thread holds its result, the wake socket and nothing else
// by shared ownership, so an abandoned lookup finishes harmlessly after the connect has returned.
struct PosixLookup
{
    std::mutex           mMutex;
    std::vector<Address> mAddresses;
    bool                 mIsDone {};
};

AttemptStatus resolve(const std::string&                       host,
                      std::uint16_t                            port,
                      std::chrono::steady_clock::time_point    deadline,
                      const std::shared_ptr<const WakeSocket>& wake,
                      const std::atomic<bool>&                 isCancelled,
                      std::vector<Address>&                    addresses,
                      std::string&                             error)
{
    const std::shared_ptr<PosixLookup> lookup { std::make_shared<PosixLookup>() };
    std::thread {
        [host, port, lookup, waker = std::shared_ptr<const WakeSocket> { wake }]()
        {
            addrinfo hints {};
            hints.ai_family   = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            addrinfo*            found {};
            std::vector<Address> resolved;
            if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) == 0)
            {
                for (const addrinfo* entry { found }; entry != nullptr; entry = entry->ai_next)
                    appendAddress(resolved, *entry);
                freeaddrinfo(found);
            }
            {
                const std::scoped_lock lock { lookup->mMutex };
                lookup->mAddresses = std::move(resolved);
                lookup->mIsDone    = true;
            }
            waker->wake();
        }
    }.detach();

    while (true)
    {
        {
            const std::scoped_lock lock { lookup->mMutex };
            if (lookup->mIsDone)
            {
                addresses = lookup->mAddresses;
                break;
            }
        }
        const std::chrono::milliseconds left { getRemaining(deadline) };
        if (isCancelled.load())
            return AttemptStatus::kCancelled;
        if (left.count() == 0)
            return AttemptStatus::kTimedOut;
        if (! waitForWake(*wake, left))
            return AttemptStatus::kFailed;
    }
    if (addresses.empty())
    {
        error = "cannot resolve " + host;
        return AttemptStatus::kFailed;
    }
    return AttemptStatus::kConnected;
}
#endif

ConnectResult makeFailure(std::string error)
{
    return ConnectResult { .mConnection = std::nullopt, .mError = std::move(error), .mIsCancelled = false };
}

ConnectResult makeCancelled()
{
    return ConnectResult { .mConnection = std::nullopt, .mError = {}, .mIsCancelled = true };
}

// One non-blocking connect to one address, polled with the wake socket until it finishes, the
// deadline passes or the caller cancels. `error` is set when it fails.
AttemptStatus attempt(const Address&                        address,
                      std::chrono::steady_clock::time_point deadline,
                      const WakeSocket&                     wake,
                      const std::atomic<bool>&              isCancelled,
                      TcpConnection&                        connection,
                      std::string&                          error)
{
    const NativeSocket socket { ::socket(address.mFamily, address.mType, address.mProtocol) };
    if (! isValid(socket))
    {
        error = describeError(getLastError());
        return AttemptStatus::kFailed;
    }
    connection = TcpConnection { toHandle(socket) };
    if (! setNonBlocking(socket))
    {
        error = describeError(getLastError());
        return AttemptStatus::kFailed;
    }
    setNoDelay(socket);

    if (::connect(socket, reinterpret_cast<const sockaddr*>(&address.mStorage), address.mLength) == 0)
        return AttemptStatus::kConnected;
    if (const int code { getLastError() }; code != kInProgress && code != kWouldBlock)
    {
        error = describeError(code);
        return AttemptStatus::kFailed;
    }

    while (true)
    {
        if (isCancelled.load())
            return AttemptStatus::kCancelled;
        const std::chrono::milliseconds left { getRemaining(deadline) };
        if (left.count() == 0)
            return AttemptStatus::kTimedOut;
        const PollResult ready { pollSockets(connection, true, &wake, left) };
        if (ready.mIsFailed)
        {
            error = describeError(getLastError());
            return AttemptStatus::kFailed;
        }
        if (ready.mIsWoken)
            wake.drain();
        if (! ready.mIsWritable && ! ready.mIsReadable && ! ready.mHasError)
            continue;
        const int code { getConnectError(socket) };
        if (code == 0 && ready.mIsWritable && ! ready.mHasError)
            return AttemptStatus::kConnected;
        // A hang-up or error with no error code is still a failed connect, never a spin.
        error = describeError(code != 0 ? code : getLastError());
        return AttemptStatus::kFailed;
    }
}
} // namespace

// -- Network -----------------------------------------------------------------------------------

Network::Network() noexcept
{
#ifdef _WIN32
    WSADATA        data {};
    constexpr WORD kVersion { MAKEWORD(2, 2) };
    mIsReady = WSAStartup(kVersion, &data) == 0;
#else
    mIsReady = true;
#endif
}

Network::~Network()
{
#ifdef _WIN32
    if (mIsReady)
        WSACleanup();
#endif
}

// -- TcpConnection -----------------------------------------------------------------------------

TcpConnection::TcpConnection(SocketHandle handle) noexcept
    : mHandle { handle }
{
}

TcpConnection::~TcpConnection()
{
    close();
}

TcpConnection::TcpConnection(TcpConnection&& other) noexcept
    : mHandle { std::exchange(other.mHandle, kInvalidSocket) }
{
}

TcpConnection& TcpConnection::operator=(TcpConnection&& other) noexcept
{
    if (this != &other)
    {
        close();
        mHandle = std::exchange(other.mHandle, kInvalidSocket);
    }
    return *this;
}

IoResult TcpConnection::read(std::span<std::byte> buffer) const noexcept
{
    if (! isOpen())
        return IoResult { .mCount = 0, .mStatus = IoStatus::kClosed };
    const int count { recvNative(toNative(mHandle), buffer.data(), buffer.size()) };
    if (count > 0)
        return IoResult { .mCount = static_cast<std::size_t>(count), .mStatus = IoStatus::kDone };
    if (count == 0)
        return IoResult { .mCount = 0, .mStatus = IoStatus::kClosed };
    const int error { getLastError() };
    return IoResult { .mCount  = 0,
                      .mStatus = error == kWouldBlock ? IoStatus::kWouldBlock : IoStatus::kError };
}

IoResult TcpConnection::write(std::span<const std::byte> bytes) const noexcept
{
    if (! isOpen())
        return IoResult { .mCount = 0, .mStatus = IoStatus::kClosed };
    if (bytes.empty())
        return IoResult {};
    const int count { sendNative(toNative(mHandle), bytes.data(), bytes.size()) };
    if (count >= 0)
        return IoResult { .mCount = static_cast<std::size_t>(count), .mStatus = IoStatus::kDone };
    const int error { getLastError() };
    return IoResult { .mCount  = 0,
                      .mStatus = error == kWouldBlock ? IoStatus::kWouldBlock : IoStatus::kError };
}

void TcpConnection::close() noexcept
{
    if (mHandle == kInvalidSocket)
        return;
    closeNative(toNative(mHandle));
    mHandle = kInvalidSocket;
}

// -- Connecting --------------------------------------------------------------------------------

ConnectResult connectTcp(const std::string&                       host,
                         std::uint16_t                            port,
                         std::chrono::milliseconds                timeout,
                         const std::shared_ptr<const WakeSocket>& wake,
                         const std::atomic<bool>&                 isCancelled)
{
    const auto           deadline { std::chrono::steady_clock::now() + timeout };
    std::string          error;
    std::vector<Address> addresses;
    switch (resolve(host, port, deadline, wake, isCancelled, addresses, error))
    {
        case AttemptStatus::kConnected:
            break;
        case AttemptStatus::kCancelled:
            return makeCancelled();
        case AttemptStatus::kTimedOut:
            return makeFailure(describeError(kTimedOut));
        case AttemptStatus::kFailed:
            return makeFailure(std::move(error));
    }

    for (std::size_t i { 0 }; i < addresses.size(); ++i)
    {
        // Each address gets its share of what is left (connection.md 1), so an address that never
        // answers cannot spend the budget of those after it.
        const std::chrono::milliseconds budget { getAttemptBudget(getRemaining(deadline),
                                                                  addresses.size() - i) };
        const auto                      attemptDeadline { std::chrono::steady_clock::now() + budget };
        TcpConnection                   connection;
        switch (attempt(addresses[i], attemptDeadline, *wake, isCancelled, connection, error))
        {
            case AttemptStatus::kConnected:
                return ConnectResult { .mConnection  = std::move(connection),
                                       .mError       = {},
                                       .mIsCancelled = false };
            case AttemptStatus::kCancelled:
                return makeCancelled();
            case AttemptStatus::kTimedOut:
                error = describeError(kTimedOut);
                break;
            case AttemptStatus::kFailed:
                break;
        }
    }
    return makeFailure(std::move(error));
}

std::chrono::milliseconds getAttemptBudget(std::chrono::milliseconds left, std::size_t addressesLeft) noexcept
{
    if (addressesLeft <= 1)
        return left;
    return left / static_cast<std::chrono::milliseconds::rep>(addressesLeft);
}

// -- WakeSocket --------------------------------------------------------------------------------

WakeSocket::~WakeSocket()
{
    if (mHandle != kInvalidSocket)
        closeNative(toNative(mHandle));
}

WakeSocket::WakeSocket(WakeSocket&& other) noexcept
    : mHandle { std::exchange(other.mHandle, kInvalidSocket) }
{
}

WakeSocket& WakeSocket::operator=(WakeSocket&& other) noexcept
{
    if (this != &other)
    {
        if (mHandle != kInvalidSocket)
            closeNative(toNative(mHandle));
        mHandle = std::exchange(other.mHandle, kInvalidSocket);
    }
    return *this;
}

std::optional<WakeSocket> WakeSocket::open()
{
    const NativeSocket socket { ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) };
    if (! isValid(socket))
        return std::nullopt;
    WakeSocket wake;
    wake.mHandle = toHandle(socket);

    sockaddr_in address {};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = 0;
    socklen_t length { sizeof(address) };
    if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0
        || getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) != 0
        || ::connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0
        || ! setNonBlocking(socket))
        return std::nullopt;
    return wake;
}

void WakeSocket::wake() const noexcept
{
    const std::byte signal { 1 };
    static_cast<void>(sendNative(toNative(mHandle), &signal, 1));
}

void WakeSocket::drain() const noexcept
{
    constexpr std::size_t             kDrainSize { 64 };
    std::array<std::byte, kDrainSize> buffer {};
    while (recvNative(toNative(mHandle), buffer.data(), buffer.size()) > 0)
    {
    }
}

// -- Polling -----------------------------------------------------------------------------------

PollResult pollSockets(const TcpConnection&      connection,
                       bool                      willWrite,
                       const WakeSocket*         wake,
                       std::chrono::milliseconds timeout) noexcept
{
    std::array<PollFd, 2> fds {};
    std::size_t           count { 0 };
    if (connection.isOpen())
    {
        fds[count].fd     = toNative(connection.getHandle());
        fds[count].events = static_cast<short>(willWrite ? (POLLIN | POLLOUT) : POLLIN);
        ++count;
    }
    const std::size_t wakeIndex { count };
    if (wake != nullptr)
    {
        fds[count].fd     = toNative(wake->getHandle());
        fds[count].events = POLLIN;
        ++count;
    }

    PollResult result;
    if (count == 0)
        return result;
    const int ready { pollNative(fds.data(), count, toTimeoutMs(timeout)) };
    if (ready < 0)
    {
        result.mIsFailed = true;
        return result;
    }
    if (ready == 0)
        return result;

    if (connection.isOpen())
    {
        const short events { fds[0].revents };
        result.mIsReadable = (events & (POLLIN | POLLHUP)) != 0;
        result.mIsWritable = (events & POLLOUT) != 0;
        result.mHasError   = (events & (POLLERR | POLLNVAL)) != 0;
    }
    if (wake != nullptr)
        result.mIsWoken = (fds[wakeIndex].revents & POLLIN) != 0;
    return result;
}

bool makeNonBlocking(SocketHandle handle) noexcept
{
    const NativeSocket socket { toNative(handle) };
    if (! setNonBlocking(socket))
        return false;
    setNoDelay(socket);
    return true;
}
} // namespace muckaii::transport
