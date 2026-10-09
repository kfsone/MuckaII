#include "muckaii/base/types.hpp"
#include "muckaii/base/wire.hpp"
#include "muckaii/transport/loopback/tcpListener.hpp"
#include "muckaii/transport/socket.hpp"
#include "muckaii/transport/telnet.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

using namespace muckaii::transport;
namespace types = muckaii::types;
namespace wire  = muckaii::wire;
using namespace std::chrono_literals;
using loopback::TcpListener;

// kZeroByte is the zero-valued byte used in expected protocol frames.
constexpr int kZeroByte { 0x00 };
// kTtypeSendCommand requests the terminal type in a TTYPE subnegotiation.
constexpr int kTtypeSendCommand { 0x01 };
// kTtypeIsCommand identifies the terminal-type response.
constexpr int kTtypeIsCommand { kZeroByte };
// kEnvironSendCommand requests NEW-ENVIRON values.
constexpr int kEnvironSendCommand { 0x01 };
// kEnvironIsCommand identifies a NEW-ENVIRON response.
constexpr int kEnvironIsCommand { kZeroByte };
// kEnvironVariableType marks the requested variable name.
constexpr int kEnvironVariableType { kZeroByte };
// kEnvironValueType marks the variable value.
constexpr int kEnvironValueType { 0x01 };
// kExpectedTerminalType is the exact TTYPE value expected from the negotiator.
constexpr char kExpectedTerminalType[] { "ansi" };
// kExpectedEnvironVariableName is the NEW-ENVIRON variable name in the reply.
constexpr char kExpectedEnvironVariableName[] { "USER" };
// kUnknownTelnetOption is an unsupported option used to check refusal behavior.
constexpr int kUnknownTelnetOption { 0x05 };
// kTelnetGoAhead is the command that the negotiator ignores.
constexpr int kTelnetGoAhead { 0xF9 };
// kNawsWidthHighByteOne is the high size byte for width 0x1FF.
constexpr int kNawsWidthHighByteOne { 0x01 };
// kNawsWidthWithIacInBothBytes exercises IAC escaping in both width bytes.
constexpr int kNawsWidthWithIacInBothBytes { 0x1FF };
// kNawsHeightWithIacInHighByte exercises IAC escaping in the height high byte.
constexpr int kNawsHeightWithIacInHighByte { 0xFF00 };
// kLoopbackAddress is the local numeric host used by socket tests.
constexpr char kLoopbackAddress[] { "127.0.0.1" };
// kLoopbackHostname is the local hostname used to exercise resolution.
constexpr char kLoopbackHostname[] { "localhost" };
// kConnectionTimedOut is the expected reason for an overall connection deadline.
constexpr char kConnectionTimedOut[] { "connection timed out" };

namespace
{
// bytes converts test values into a telnet byte sequence.
types::Bytes bytes(std::initializer_list<int> values)
{
    types::Bytes out;
    for (const int value : values)
        out.push_back(std::byte { static_cast<std::uint8_t>(value) });
    return out;
}

// text converts test text into its exact transmitted bytes.
types::Bytes text(std::string_view value)
{
    types::Bytes out;
    for (const char c : value)
        out.push_back(std::byte { static_cast<std::uint8_t>(c) });
    return out;
}

// emptyNewEnvironRequest builds a SEND request with no requested variable list.
types::Bytes emptyNewEnvironRequest()
{
    return bytes({ wire::kTelnetIac,
                   wire::kTelnetSb,
                   kOptionNewEnviron,
                   kEnvironSendCommand,
                   wire::kTelnetIac,
                   wire::kTelnetSe });
}

// requestedNewEnvironRequest builds a SEND that requests the USER variable.
types::Bytes requestedNewEnvironRequest()
{
    types::Bytes request = bytes(
        { wire::kTelnetIac, wire::kTelnetSb, kOptionNewEnviron, kEnvironSendCommand, kEnvironVariableType });
    const types::Bytes variableName { text(kExpectedEnvironVariableName) };
    request.insert(request.end(), variableName.begin(), variableName.end());
    request.push_back(std::byte { wire::kTelnetIac });
    request.push_back(std::byte { wire::kTelnetSe });
    return request;
}

// ttypeSendRequest builds the TTYPE SEND packet used by negotiation tests.
types::Bytes ttypeSendRequest()
{
    return bytes({ wire::kTelnetIac,
                   wire::kTelnetSb,
                   kOptionTtype,
                   kTtypeSendCommand,
                   wire::kTelnetIac,
                   wire::kTelnetSe });
}

// ttypeReply builds the exact TTYPE IS packet expected from the negotiator.
types::Bytes ttypeReply()
{
    types::Bytes       reply { bytes({ wire::kTelnetIac, wire::kTelnetSb, kOptionTtype, kTtypeIsCommand }) };
    const types::Bytes terminalType { text(kExpectedTerminalType) };
    reply.insert(reply.end(), terminalType.begin(), terminalType.end());
    reply.push_back(std::byte { wire::kTelnetIac });
    reply.push_back(std::byte { wire::kTelnetSe });
    return reply;
}

// newEnvironUserReply builds the exact USER reply expected from NEW-ENVIRON negotiation.
types::Bytes newEnvironUserReply(std::string_view loginName)
{
    types::Bytes       reply { bytes(
        { wire::kTelnetIac, wire::kTelnetSb, kOptionNewEnviron, kEnvironIsCommand, kEnvironVariableType }) };
    const types::Bytes variableName { text(kExpectedEnvironVariableName) };
    const types::Bytes userName { text(loginName) };
    reply.insert(reply.end(), variableName.begin(), variableName.end());
    reply.push_back(std::byte { kEnvironValueType });
    reply.insert(reply.end(), userName.begin(), userName.end());
    reply.push_back(std::byte { wire::kTelnetIac });
    reply.push_back(std::byte { wire::kTelnetSe });
    return reply;
}

// openWake creates the shared wake channel required by connection tests.
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
// fillBacklog keeps a listener backlog occupied so a connect remains pending.
std::vector<TcpConnection> fillBacklog(std::uint16_t port, const std::shared_ptr<const WakeSocket>& wake)
{
    // kMaxFillers bounds the number of open sockets used to occupy the backlog.
    constexpr int              kMaxFillers { 8 };
    const std::atomic<bool>    isCancelled {};
    std::vector<TcpConnection> fillers;
    for (int i { 0 }; i < kMaxFillers; ++i)
    {
        ConnectResult filler { connectTcp(kLoopbackAddress, port, 200ms, wake, isCancelled) };
        if (! filler.mConnection.has_value())
            break;
        fillers.push_back(std::move(*filler.mConnection));
    }
    return fillers;
}

// connectPending runs one pending connection attempt for the cancellation test.
void connectPending(std::uint16_t                     port,
                    std::shared_ptr<const WakeSocket> wake,
                    std::atomic<bool>&                isCancelled,
                    ConnectResult&                    result)
{
    result = connectTcp(kLoopbackAddress, port, 15s, wake, isCancelled);
}
} // namespace

// -- Telnet negotiation (docs/spec/connection.md 2, with its default: lines) ----------------------

// TelnetNegotiator checks the client refuses a request to enable ECHO.
TEST(TelnetNegotiator, RefusesDoEcho)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionEcho })),
              bytes({ wire::kTelnetIac, wire::kTelnetWont, kOptionEcho }));
}

// TelnetNegotiator checks the client refuses a request to enable SGA.
TEST(TelnetNegotiator, RefusesDoSgaAsClioDoes)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionSga })),
              bytes({ wire::kTelnetIac, wire::kTelnetWont, kOptionSga }));
}

// TelnetNegotiator checks the client agrees to TTYPE only once.
TEST(TelnetNegotiator, AcceptsTtypeOnce)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionTtype })),
              bytes({ wire::kTelnetIac, wire::kTelnetWill, kOptionTtype }));
    EXPECT_TRUE(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionTtype })).empty());
}

// TelnetNegotiator checks TTYPE SEND receives a reply only after WILL TTYPE.
TEST(TelnetNegotiator, AnswersTtypeSendOnlyAfterWill)
{
    TelnetNegotiator   telnet { 80 };
    const types::Bytes send { ttypeSendRequest() };
    EXPECT_TRUE(telnet.onCommand(send).empty());
    static_cast<void>(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionTtype })));
    EXPECT_EQ(telnet.onCommand(send), ttypeReply());
}

// TelnetNegotiator checks NAWS is agreed once and reports the current columns.
TEST(TelnetNegotiator, AcceptsNawsOnceWithTheColumnCount)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionNaws })),
              bytes({ wire::kTelnetIac,
                      wire::kTelnetWill,
                      kOptionNaws,
                      wire::kTelnetIac,
                      wire::kTelnetSb,
                      kOptionNaws,
                      kZeroByte,
                      80,
                      kZeroByte,
                      21,
                      wire::kTelnetIac,
                      wire::kTelnetSe }));
    EXPECT_TRUE(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionNaws })).empty());
    EXPECT_TRUE(telnet.isNawsAgreed());
}

// TelnetNegotiator checks an empty login name refuses NEW-ENVIRON.
TEST(TelnetNegotiator, RefusesNewEnvironAsClioDoes)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionNewEnviron })),
              bytes({ wire::kTelnetIac, wire::kTelnetWont, kOptionNewEnviron }));
}

// TelnetNegotiator checks an explicitly empty login name refuses NEW-ENVIRON.
TEST(TelnetNegotiator, RefusesNewEnvironWhenTheLoginNameIsExplicitlyEmpty)
{
    // The two-arg constructor with an empty name behaves exactly as the one-arg constructor: no
    // one shot is skipped, Clio's WONT every time (connection.md 2.1 default).
    TelnetNegotiator telnet { 80, std::string {} };
    EXPECT_EQ(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionNewEnviron })),
              bytes({ wire::kTelnetIac, wire::kTelnetWont, kOptionNewEnviron }));
}

// TelnetNegotiator checks a non-empty login name enables NEW-ENVIRON only once.
TEST(TelnetNegotiator, AcceptsNewEnvironOnceWhenTheLoginNameIsNonEmpty)
{
    TelnetNegotiator telnet { 80, std::string { "mudx" } };
    EXPECT_EQ(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionNewEnviron })),
              bytes({ wire::kTelnetIac, wire::kTelnetWill, kOptionNewEnviron }));
    // First time only, like TTYPE and NAWS.
    EXPECT_TRUE(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionNewEnviron })).empty());
}

// TelnetNegotiator checks NEW-ENVIRON SEND returns the USER value after WILL.
TEST(TelnetNegotiator, AnswersNewEnvironSendWithIsUserAfterWill)
{
    TelnetNegotiator telnet { 80, std::string { "mudx" } };
    const auto       send { emptyNewEnvironRequest() };
    static_cast<void>(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionNewEnviron })));
    EXPECT_EQ(telnet.onCommand(send), newEnvironUserReply("mudx"));
    // A second SEND still gets an answer: unlike the one-shot DO guard, replying to SEND does not
    // consume a guard of its own.
    EXPECT_EQ(telnet.onCommand(send), newEnvironUserReply("mudx"));
}

// TelnetNegotiator checks an empty login name receives no NEW-ENVIRON SEND reply.
TEST(TelnetNegotiator, IgnoresNewEnvironSendWhenTheLoginNameIsEmpty)
{
    // WONT was sent, so a server that ignores it and asks SEND anyway gets nothing back.
    TelnetNegotiator telnet { 80 };
    static_cast<void>(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionNewEnviron })));
    EXPECT_TRUE(telnet.onCommand(emptyNewEnvironRequest()).empty());
}

// TelnetNegotiator checks requested variable lists still receive the USER value.
TEST(TelnetNegotiator, NewEnvironSendWithRequestedVariableNamesIsStillAnswered)
{
    // The SEND may list variable names; MuckaII still answers with USER (connection.md 2.1/2.2).
    TelnetNegotiator telnet { 80, std::string { "mud" } };
    static_cast<void>(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionNewEnviron })));
    const types::Bytes send { requestedNewEnvironRequest() };
    EXPECT_EQ(telnet.onCommand(send), newEnvironUserReply("mud"));
}

// TelnetNegotiator checks reset rearms the NEW-ENVIRON one-shot response.
TEST(TelnetNegotiator, NewEnvironOneShotGuardRearmsOnReset)
{
    TelnetNegotiator telnet { 80, std::string { "mudx" } };
    static_cast<void>(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionNewEnviron })));
    telnet.reset();
    EXPECT_EQ(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionNewEnviron })),
              bytes({ wire::kTelnetIac, wire::kTelnetWill, kOptionNewEnviron }));
}

// TelnetNegotiator checks unsupported DO and WILL options are refused.
TEST(TelnetNegotiator, RefusesOtherDoAndWill)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kUnknownTelnetOption })),
              bytes({ wire::kTelnetIac, wire::kTelnetWont, kUnknownTelnetOption }));
    EXPECT_EQ(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetWill, kUnknownTelnetOption })),
              bytes({ wire::kTelnetIac, wire::kTelnetDont, kUnknownTelnetOption }));
}

// TelnetNegotiator checks WILL ECHO, DONT, WONT, and GA receive no reply.
TEST(TelnetNegotiator, IgnoresWillEchoDontWontAndGa)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_TRUE(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetWill, kOptionEcho })).empty());
    EXPECT_TRUE(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDont, kOptionEcho })).empty());
    EXPECT_TRUE(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetWont, kOptionEcho })).empty());
    EXPECT_TRUE(telnet.onCommand(bytes({ wire::kTelnetIac, kTelnetGoAhead })).empty());
}

// TelnetNegotiator checks WILL SGA is answered once per connection.
TEST(TelnetNegotiator, AnswersWillSgaOncePerConnection)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_EQ(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetWill, kOptionSga })),
              bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionSga }));
    EXPECT_TRUE(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetWill, kOptionSga })).empty());
    telnet.reset();
    EXPECT_EQ(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetWill, kOptionSga })),
              bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionSga }));
}

// TelnetNegotiator checks NAWS is resent only after agreement and a width change.
TEST(TelnetNegotiator, ResendsNawsOnlyWhenAgreedAndChanged)
{
    TelnetNegotiator telnet { 80 };
    EXPECT_TRUE(telnet.setColumns(60).empty());
    EXPECT_FALSE(telnet.findNawsForClientMode().has_value());
    static_cast<void>(telnet.onCommand(bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionNaws })));
    EXPECT_TRUE(telnet.setColumns(60).empty());
    EXPECT_EQ(telnet.setColumns(100),
              bytes({ wire::kTelnetIac,
                      wire::kTelnetSb,
                      kOptionNaws,
                      kZeroByte,
                      100,
                      kZeroByte,
                      21,
                      wire::kTelnetIac,
                      wire::kTelnetSe }));
    EXPECT_EQ(telnet.findNawsForClientMode(),
              bytes({ wire::kTelnetIac,
                      wire::kTelnetSb,
                      kOptionNaws,
                      kZeroByte,
                      100,
                      kZeroByte,
                      21,
                      wire::kTelnetIac,
                      wire::kTelnetSe }));
}

// Outbound IAC escaping: a 255 among the size bytes is sent doubled (connection.md 2.2).
// TelnetNegotiator checks NAWS doubles IAC bytes in its dimensions.
TEST(TelnetNegotiator, NawsDoublesAnIacByte)
{
    EXPECT_EQ(makeNaws(wire::kTelnetIac, kNawsRows),
              bytes({ wire::kTelnetIac,
                      wire::kTelnetSb,
                      kOptionNaws,
                      kZeroByte,
                      wire::kTelnetIac,
                      wire::kTelnetIac,
                      kZeroByte,
                      21,
                      wire::kTelnetIac,
                      wire::kTelnetSe }));
    EXPECT_EQ(makeNaws(kNawsWidthWithIacInBothBytes, kNawsHeightWithIacInHighByte),
              bytes({ wire::kTelnetIac,
                      wire::kTelnetSb,
                      kOptionNaws,
                      kNawsWidthHighByteOne,
                      wire::kTelnetIac,
                      wire::kTelnetIac,
                      wire::kTelnetIac,
                      wire::kTelnetIac,
                      kZeroByte,
                      wire::kTelnetIac,
                      wire::kTelnetSe }));
}

// -- Sockets on the loopback interface ------------------------------------------------------------

// TcpConnection checks loopback connections transfer bytes and detect peer closure.
TEST(TcpConnection, ConnectsWritesAndReadsOverLoopback)
{
    const Network network;
    ASSERT_TRUE(network.isReady());
    const std::shared_ptr<const WakeSocket> wake { openWake() };
    ASSERT_NE(wake, nullptr);
    std::optional<TcpListener> listener { TcpListener::listen() };
    ASSERT_TRUE(listener.has_value());

    const std::atomic<bool> isCancelled {};
    ConnectResult client { connectTcp(kLoopbackAddress, listener->getPort(), 5s, wake, isCancelled) };
    ASSERT_TRUE(client.mConnection.has_value()) << client.mError;
    std::optional<TcpConnection> server { listener->accept(5s) };
    ASSERT_TRUE(server.has_value());

    const types::Bytes message { text("hello\r\n") };
    EXPECT_EQ(client.mConnection->write(message).mCount, message.size());

    types::Bytes received;
    types::Bytes buffer(64);
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

// TcpConnection checks a refused connection returns a failure reason.
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
    const ConnectResult     result { connectTcp(kLoopbackAddress, port, 10s, wake, isCancelled) };
    EXPECT_FALSE(result.mConnection.has_value());
    EXPECT_FALSE(result.mIsCancelled);
    EXPECT_FALSE(result.mError.empty());
}

// A cancel during a connect that would otherwise hang returns at once, not at the timeout.
// TcpConnection checks cancellation interrupts a pending connection promptly.
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
    std::thread       connector {
        connectPending, listener->getPort(), wake, std::ref(isCancelled), std::ref(result)
    };
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
// TcpConnection checks the timeout bounds the entire connection attempt.
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
    const ConnectResult     result { connectTcp(
        kLoopbackAddress, listener->getPort(), 300ms, wake, isCancelled) };
    const auto              took { std::chrono::steady_clock::now() - started };

    EXPECT_FALSE(result.mConnection.has_value());
    EXPECT_FALSE(result.mIsCancelled);
    EXPECT_EQ(result.mError, kConnectionTimedOut);
    EXPECT_GE(took, 250ms);
    EXPECT_LT(took, 1500ms);
}

// TcpConnection checks remaining time is divided among untried addresses.
TEST(TcpConnection, EachAddressGetsItsShareOfTheBudget)
{
    EXPECT_EQ(getAttemptBudget(15000ms, 3), 5000ms);
    EXPECT_EQ(getAttemptBudget(10000ms, 2), 5000ms);
    EXPECT_EQ(getAttemptBudget(7000ms, 1), 7000ms);
    EXPECT_EQ(getAttemptBudget(7000ms, 0), 7000ms);
}

// A name goes through the asynchronous resolver; kLoopbackHostname never leaves the machine.
// TcpConnection checks hostnames resolve before a loopback connection is attempted.
TEST(TcpConnection, ConnectsByName)
{
    const Network                           network;
    const std::shared_ptr<const WakeSocket> wake { openWake() };
    ASSERT_NE(wake, nullptr);
    std::optional<TcpListener> listener { TcpListener::listen() };
    ASSERT_TRUE(listener.has_value());
    const std::atomic<bool> isCancelled {};
    const ConnectResult client { connectTcp(kLoopbackHostname, listener->getPort(), 10s, wake, isCancelled) };
    ASSERT_TRUE(client.mConnection.has_value()) << client.mError;
    EXPECT_TRUE(listener->accept(5s).has_value());
}

// WakeSocket checks a wake from another thread interrupts polling.
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
