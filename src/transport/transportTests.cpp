#include "muckaii/transport/loopback/tcpListener.hpp"
#include "muckaii/transport/socket.hpp"
#include "muckaii/transport/telnet.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace muckaii::transport;
using namespace std::chrono_literals;
using loopback::TcpListener;

namespace
{
std::vector<std::byte> bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> out;
    for (const int value : values)
        out.push_back(std::byte { static_cast<std::uint8_t>(value) });
    return out;
}

std::vector<std::byte> text(std::string_view value)
{
    std::vector<std::byte> out;
    for (const char c : value)
        out.push_back(std::byte { static_cast<std::uint8_t>(c) });
    return out;
}

std::shared_ptr<const WakeSocket> openWake()
{
    std::optional<WakeSocket> wake { WakeSocket::open() };
    if (! wake.has_value())
        return nullptr;
    return std::make_shared<const WakeSocket>(std::move(*wake));
}

// Fills a listener's backlog, so the next connect to it neither completes nor is refused at once:
// Windows retries the SYN for about 2 s before refusing, POSIX drops it. The fillers are returned
// so they stay open.
std::vector<TcpConnection> fillBacklog(std::uint16_t port, const std::shared_ptr<const WakeSocket>& wake)
{
    constexpr int              kMaxFillers { 8 };
    const std::atomic<bool>    isCancelled {};
    std::vector<TcpConnection> fillers;
    for (int i { 0 }; i < kMaxFillers; ++i)
    {
        ConnectResult filler { connectTcp("127.0.0.1", port, 200ms, wake, isCancelled) };
        if (! filler.mConnection.has_value())
            break;
        fillers.push_back(std::move(*filler.mConnection));
    }
    return fillers;
}
} // namespace

// -- Telnet negotiation (docs/spec/connection.md 2, with its default: lines) ----------------------

TEST(TelnetNegotiator, RefusesDoEcho)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ 0xFF, 0xFD, 0x01 })), bytes({ 0xFF, 0xFC, 0x01 }));
}

TEST(TelnetNegotiator, RefusesDoSgaAsClioDoes)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ 0xFF, 0xFD, 0x03 })), bytes({ 0xFF, 0xFC, 0x03 }));
}

TEST(TelnetNegotiator, AcceptsTtypeOnce)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ 0xFF, 0xFD, 0x18 })), bytes({ 0xFF, 0xFB, 0x18 }));
    EXPECT_TRUE(telnet.onCommand(bytes({ 0xFF, 0xFD, 0x18 })).empty());
}

TEST(TelnetNegotiator, AnswersTtypeSendOnlyAfterWill)
{
    TelnetNegotiator telnet { 80 };
    const auto       send { bytes({ 0xFF, 0xFA, 0x18, 0x01, 0xFF, 0xF0 }) };
    EXPECT_TRUE(telnet.onCommand(send).empty());
    static_cast<void>(telnet.onCommand(bytes({ 0xFF, 0xFD, 0x18 })));
    EXPECT_EQ(telnet.onCommand(send), bytes({ 0xFF, 0xFA, 0x18, 0x00, 'a', 'n', 's', 'i', 0xFF, 0xF0 }));
}

TEST(TelnetNegotiator, AcceptsNawsOnceWithTheColumnCount)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ 0xFF, 0xFD, 0x1F })),
              bytes({ 0xFF, 0xFB, 0x1F, 0xFF, 0xFA, 0x1F, 0x00, 80, 0x00, 21, 0xFF, 0xF0 }));
    EXPECT_TRUE(telnet.onCommand(bytes({ 0xFF, 0xFD, 0x1F })).empty());
    EXPECT_TRUE(telnet.isNawsAgreed());
}

TEST(TelnetNegotiator, RefusesNewEnvironAsClioDoes)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ 0xFF, 0xFD, 0x27 })), bytes({ 0xFF, 0xFC, 0x27 }));
}

TEST(TelnetNegotiator, RefusesOtherDoAndWill)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ 0xFF, 0xFD, 0x05 })), bytes({ 0xFF, 0xFC, 0x05 }));
    EXPECT_EQ(telnet.onCommand(bytes({ 0xFF, 0xFB, 0x05 })), bytes({ 0xFF, 0xFE, 0x05 }));
}

TEST(TelnetNegotiator, IgnoresWillEchoDontWontAndGa)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_TRUE(telnet.onCommand(bytes({ 0xFF, 0xFB, 0x01 })).empty());
    EXPECT_TRUE(telnet.onCommand(bytes({ 0xFF, 0xFE, 0x01 })).empty());
    EXPECT_TRUE(telnet.onCommand(bytes({ 0xFF, 0xFC, 0x01 })).empty());
    EXPECT_TRUE(telnet.onCommand(bytes({ 0xFF, 0xF9 })).empty());
}

TEST(TelnetNegotiator, AnswersWillSgaOncePerConnection)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ 0xFF, 0xFB, 0x03 })), bytes({ 0xFF, 0xFD, 0x03 }));
    EXPECT_TRUE(telnet.onCommand(bytes({ 0xFF, 0xFB, 0x03 })).empty());
    telnet.reset();
    EXPECT_EQ(telnet.onCommand(bytes({ 0xFF, 0xFB, 0x03 })), bytes({ 0xFF, 0xFD, 0x03 }));
}

TEST(TelnetNegotiator, ResendsNawsOnlyWhenAgreedAndChanged)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_TRUE(telnet.setColumns(60).empty());
    EXPECT_FALSE(telnet.findNawsForClientMode().has_value());
    static_cast<void>(telnet.onCommand(bytes({ 0xFF, 0xFD, 0x1F })));
    EXPECT_TRUE(telnet.setColumns(60).empty());
    EXPECT_EQ(telnet.setColumns(100), bytes({ 0xFF, 0xFA, 0x1F, 0x00, 100, 0x00, 21, 0xFF, 0xF0 }));
    EXPECT_EQ(telnet.findNawsForClientMode(), bytes({ 0xFF, 0xFA, 0x1F, 0x00, 100, 0x00, 21, 0xFF, 0xF0 }));
}

// Outbound IAC escaping: a 255 among the size bytes is sent doubled (connection.md 2.2).
TEST(TelnetNegotiator, NawsDoublesAnIacByte)
{
    EXPECT_EQ(makeNaws(255, 21), bytes({ 0xFF, 0xFA, 0x1F, 0x00, 0xFF, 0xFF, 0x00, 21, 0xFF, 0xF0 }));
    EXPECT_EQ(makeNaws(0x1FF, 0xFF00),
              bytes({ 0xFF, 0xFA, 0x1F, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xF0 }));
}

// -- Sockets on the loopback interface ------------------------------------------------------------

TEST(TcpConnection, ConnectsWritesAndReadsOverLoopback)
{
    const Network network;
    ASSERT_TRUE(network.isReady());
    const std::shared_ptr<const WakeSocket> wake { openWake() };
    ASSERT_NE(wake, nullptr);
    std::optional<TcpListener> listener { TcpListener::listen() };
    ASSERT_TRUE(listener.has_value());

    const std::atomic<bool> isCancelled {};
    ConnectResult           client { connectTcp("127.0.0.1", listener->getPort(), 5s, wake, isCancelled) };
    ASSERT_TRUE(client.mConnection.has_value()) << client.mError;
    std::optional<TcpConnection> server { listener->accept(5s) };
    ASSERT_TRUE(server.has_value());

    const std::vector<std::byte> message { text("hello\r\n") };
    EXPECT_EQ(client.mConnection->write(message).mCount, message.size());

    std::vector<std::byte> received;
    std::vector<std::byte> buffer(64);
    while (received.size() < message.size())
    {
        static_cast<void>(pollSockets(*server, false, nullptr, 1s));
        const IoResult result { server->read(buffer) };
        ASSERT_NE(result.mStatus, IoStatus::kError);
        ASSERT_NE(result.mStatus, IoStatus::kClosed);
        received.insert(
            received.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(result.mCount));
    }
    EXPECT_EQ(received, message);

    server->close();
    IoResult result {};
    for (int i { 0 }; i < 50 && result.mStatus != IoStatus::kClosed; ++i)
    {
        static_cast<void>(pollSockets(*client.mConnection, false, nullptr, 100ms));
        result = client.mConnection->read(buffer);
    }
    EXPECT_EQ(result.mStatus, IoStatus::kClosed);
}

TEST(TcpConnection, ReportsARefusedConnection)
{
    const Network                           network;
    const std::shared_ptr<const WakeSocket> wake { openWake() };
    ASSERT_NE(wake, nullptr);
    std::optional<TcpListener> listener { TcpListener::listen() };
    ASSERT_TRUE(listener.has_value());
    const std::uint16_t port { listener->getPort() };
    listener.reset();

    const std::atomic<bool> isCancelled {};
    const ConnectResult     result { connectTcp("127.0.0.1", port, 10s, wake, isCancelled) };
    EXPECT_FALSE(result.mConnection.has_value());
    EXPECT_FALSE(result.mIsCancelled);
    EXPECT_FALSE(result.mError.empty());
}

// A cancel during a connect that would otherwise hang returns at once, not at the timeout.
TEST(TcpConnection, CancelEndsAPendingConnectAtOnce)
{
    const Network                           network;
    const std::shared_ptr<const WakeSocket> wake { openWake() };
    ASSERT_NE(wake, nullptr);
    std::optional<TcpListener> listener { TcpListener::listen() };
    ASSERT_TRUE(listener.has_value());
    const std::vector<TcpConnection> fillers { fillBacklog(listener->getPort(), wake) };
    ASSERT_FALSE(fillers.empty());

    std::atomic<bool> isCancelled {};
    ConnectResult     result;
    std::thread       connector { [&]()
                                  {
                                result = connectTcp("127.0.0.1", listener->getPort(), 15s, wake, isCancelled);
                            } };
    std::this_thread::sleep_for(300ms);
    const auto cancelledAt { std::chrono::steady_clock::now() };
    isCancelled.store(true);
    wake->wake();
    connector.join();
    const auto took { std::chrono::steady_clock::now() - cancelledAt };

    EXPECT_TRUE(result.mIsCancelled) << result.mError;
    EXPECT_FALSE(result.mConnection.has_value());
    EXPECT_LT(took, 500ms);
}

// The timeout bounds the whole connect (connection.md 1), not each address.
TEST(TcpConnection, TheTimeoutIsOverall)
{
    const Network                           network;
    const std::shared_ptr<const WakeSocket> wake { openWake() };
    ASSERT_NE(wake, nullptr);
    std::optional<TcpListener> listener { TcpListener::listen() };
    ASSERT_TRUE(listener.has_value());
    const std::vector<TcpConnection> fillers { fillBacklog(listener->getPort(), wake) };
    ASSERT_FALSE(fillers.empty());

    const std::atomic<bool> isCancelled {};
    const auto              started { std::chrono::steady_clock::now() };
    const ConnectResult     result { connectTcp("127.0.0.1", listener->getPort(), 300ms, wake, isCancelled) };
    const auto              took { std::chrono::steady_clock::now() - started };

    EXPECT_FALSE(result.mConnection.has_value());
    EXPECT_FALSE(result.mIsCancelled);
    EXPECT_EQ(result.mError, "connection timed out");
    EXPECT_GE(took, 250ms);
    EXPECT_LT(took, 1500ms);
}

TEST(TcpConnection, EachAddressGetsItsShareOfTheBudget)
{
    EXPECT_EQ(getAttemptBudget(15000ms, 3), 5000ms);
    EXPECT_EQ(getAttemptBudget(10000ms, 2), 5000ms);
    EXPECT_EQ(getAttemptBudget(7000ms, 1), 7000ms);
    EXPECT_EQ(getAttemptBudget(7000ms, 0), 7000ms);
}

// A name goes through the asynchronous resolver; "localhost" never leaves the machine.
TEST(TcpConnection, ConnectsByName)
{
    const Network                           network;
    const std::shared_ptr<const WakeSocket> wake { openWake() };
    ASSERT_NE(wake, nullptr);
    std::optional<TcpListener> listener { TcpListener::listen() };
    ASSERT_TRUE(listener.has_value());
    const std::atomic<bool> isCancelled {};
    const ConnectResult     client { connectTcp("localhost", listener->getPort(), 10s, wake, isCancelled) };
    ASSERT_TRUE(client.mConnection.has_value()) << client.mError;
    EXPECT_TRUE(listener->accept(5s).has_value());
}

TEST(WakeSocket, WakesAPollFromAnotherThread)
{
    const Network             network;
    std::optional<WakeSocket> wake { WakeSocket::open() };
    ASSERT_TRUE(wake.has_value());
    const TcpConnection none;

    std::thread waker { [&wake]()
                        {
                            std::this_thread::sleep_for(50ms);
                            wake->wake();
                        } };
    const PollResult result { pollSockets(none, false, &*wake, 5s) };
    waker.join();
    EXPECT_TRUE(result.mIsWoken);
    wake->drain();
    EXPECT_FALSE(pollSockets(none, false, &*wake, 0ms).mIsWoken);
}
