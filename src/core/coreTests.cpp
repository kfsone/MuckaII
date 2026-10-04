#include "muckaii/core/core.hpp"
#include "muckaii/core/fakeServer/fakeMudServer.hpp"
#include "muckaii/persistence/wireLog.hpp"
#include "muckaii/transport/loopback/tcpListener.hpp"

#include <gtest/gtest.h>

#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Integration tests at the core boundary, against the loopback fake server only: the login drive
// of docs/spec/connection.md 3, telnet per section 2, client-mode entry, game-mode entry from a
// replayed capture, the wire log's masking, and Invariant #1 (root CLAUDE.md) at the socket.

using namespace muckaii;
using namespace std::chrono_literals;
namespace fake = muckaii::core::fake;

namespace
{
const std::filesystem::path kRepoRoot { MUCKAII_REPO_ROOT };

// Test credentials. Nothing here is a real account.
constexpr std::string_view kAccount { "Z1234567" };
constexpr std::string_view kPassword { "secretpw" };
constexpr std::string_view kPersona { "Kayfez" };

std::vector<std::byte> bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> out;
    for (const int value : values)
        out.push_back(std::byte { static_cast<std::uint8_t>(value) });
    return out;
}

std::vector<std::byte> concat(std::initializer_list<std::vector<std::byte>> parts)
{
    std::vector<std::byte> out;
    for (const std::vector<std::byte>& part : parts)
        out.insert(out.end(), part.begin(), part.end());
    return out;
}

std::vector<std::byte> text(std::string_view value)
{
    std::vector<std::byte> out;
    for (const char c : value)
        out.push_back(std::byte { static_cast<std::uint8_t>(c) });
    return out;
}

std::string toString(std::span<const std::byte> value)
{
    std::string out;
    for (const std::byte byte : value)
        out.push_back(static_cast<char>(byte));
    return out;
}

// The login transcript, built from connection.md: telnet options (2.1), the account and password
// prompts (3.1, the account prompt split across two reads), the Option menu and client-mode entry
// (3.2), its 95 block and width notice, persona selection typed by the player (3.4), then the game
// replayed from a capture.
std::vector<fake::ScriptStep> makeLoginScript()
{
    std::vector<fake::ScriptStep> script {
        // DO TTYPE, DO NAWS, WILL SGA, WILL ECHO.
        fake::send(bytes({ 0xFF, 0xFD, 0x18, 0xFF, 0xFD, 0x1F, 0xFF, 0xFB, 0x03, 0xFF, 0xFB, 0x01 })),
        // WILL TTYPE; WILL NAWS and NAWS 80 x 21; DO SGA. WILL ECHO gets nothing.
        fake::expect(bytes({ 0xFF,
                             0xFB,
                             0x18,
                             0xFF,
                             0xFB,
                             0x1F,
                             0xFF,
                             0xFA,
                             0x1F,
                             0x00,
                             80,
                             0x00,
                             21,
                             0xFF,
                             0xF0,
                             0xFF,
                             0xFD,
                             0x03 })),
        // TTYPE SEND gets TTYPE IS "ansi".
        fake::send(bytes({ 0xFF, 0xFA, 0x18, 0x01, 0xFF, 0xF0 })),
        fake::expect(bytes({ 0xFF, 0xFA, 0x18, 0x00, 'a', 'n', 's', 'i', 0xFF, 0xF0 })),
        fake::send("\r\nWelcome to the fake MUD2 server.\r\n\r\nAccount "),
        fake::pause(150ms),
        fake::send("ID: "),
        fake::expect(std::string { kAccount } + "\r\n"),
        fake::send(std::string { kAccount } + "\r\nPassword: "),
        fake::expect(std::string { kPassword } + "\r\n"),
        fake::send("\r\n" + std::string { kAccount }
                   + " logged in on tty1.\r\n[Checking mail...]\r\nThis reset is number 1.\r\n\r\nOption (H "
                     "for help): "),
        // NAWS again, then $-[ ESC ^F $-N/T80$-].
        fake::expect(bytes({ 0xFF, 0xFA, 0x1F, 0x00, 80,   0x00, 21,  0xFF, 0xF0, 0x1B, 0x2D, 0x5B,
                             0x1B, 0x06, 0x1B, 0x2D, 0x4E, '/',  'T', '8',  '0',  0x1B, 0x2D, 0x5D })),
        // <95> five lines </>, then ESC-80W and its notice, then the Option menu again.
        fake::send(
            concat({ bytes({ 0xFA, 0xFF, 0xFF }),
                     text("Unlicensed product!\r\n1\r\n4\r\n" + std::string { kAccount } + "\r\n1\r\n"),
                     bytes({ 0xFF, 0xFF }),
                     text("\x1b-80W[New terminal width is 80]\r\nOption (H for help): ") })),
        fake::expect("p\r\n"),
        fake::send("p\r\nPersonae available to you are: " + std::string { kPersona }
                   + ".\r\nBy what name shall I call you? "),
        fake::expect(std::string { kPersona } + "\r\n"),
    };
    const std::vector<fake::ScriptStep> game { fake::replayCapture(
        kRepoRoot / "docs" / "evidence" / "captures" / "dark-two-rats.c1", 5ms) };
    script.insert(script.end(), game.begin(), game.end());
    return script;
}

class Collector
{
public:
    core::EventSink getSink()
    {
        return [this](std::vector<session::SessionEvent> batch)
        {
            {
                const std::scoped_lock lock { mMutex };
                for (session::SessionEvent& event : batch)
                    mEvents.push_back(std::move(event));
            }
            mChanged.notify_all();
        };
    }

    bool waitFor(const std::function<bool(const session::SessionEvent&)>& predicate,
                 std::chrono::milliseconds                                timeout)
    {
        std::unique_lock lock { mMutex };
        return mChanged.wait_for(lock,
                                 timeout,
                                 [this, &predicate]()
                                 {
                                     return std::ranges::any_of(mEvents, predicate);
                                 });
    }

    std::vector<session::SessionEvent> getEvents()
    {
        const std::scoped_lock lock { mMutex };
        return mEvents;
    }

private:
    std::mutex                         mMutex;
    std::condition_variable            mChanged;
    std::vector<session::SessionEvent> mEvents;
};

bool isClientMode(const session::SessionEvent& event)
{
    const auto* mud { std::get_if<mud::Event>(&event) };
    return mud != nullptr && std::holds_alternative<mud::ClientModeEvent>(mud->mPayload);
}

bool isGameFlow(const session::SessionEvent& event)
{
    const auto* flow { std::get_if<session::FlowEvent>(&event) };
    return flow != nullptr && flow->mState == session::FlowState::kGame;
}

bool isClosed(const session::SessionEvent& event)
{
    const auto* connection { std::get_if<session::ConnectionEvent>(&event) };
    return connection != nullptr && connection->mState == session::ConnectionState::kClosed;
}

std::filesystem::path makeTempDatabase(const std::string& name)
{
    const std::filesystem::path directory { std::filesystem::temp_directory_path() / "muckaiiCoreTests" };
    std::filesystem::create_directories(directory);
    std::error_code error;
    for (const std::string suffix : { ".db", ".db-wal", ".db-shm" })
        std::filesystem::remove(directory / (name + suffix), error);
    return directory / (name + ".db");
}

struct LogRow
{
    std::string mData;
    int         mDirection {};
};

std::vector<LogRow> readWireLog(const std::filesystem::path& path)
{
    sqlite3* database {};
    EXPECT_EQ(sqlite3_open_v2(persistence::toUtf8(path).c_str(), &database, SQLITE_OPEN_READONLY, nullptr),
              SQLITE_OK);
    sqlite3_stmt* query {};
    EXPECT_EQ(sqlite3_prepare_v2(
                  database, "SELECT direction, data FROM wire_log ORDER BY run, seq;", -1, &query, nullptr),
              SQLITE_OK);
    std::vector<LogRow> rows;
    while (sqlite3_step(query) == SQLITE_ROW)
    {
        const auto* data { static_cast<const char*>(sqlite3_column_blob(query, 1)) };
        rows.push_back(
            LogRow { .mData      = std::string { data != nullptr ? data : "",
                                                 static_cast<std::size_t>(sqlite3_column_bytes(query, 1)) },
                     .mDirection = sqlite3_column_int(query, 0) });
    }
    sqlite3_finalize(query);
    sqlite3_close(database);
    return rows;
}

session::Credentials makeCredentials()
{
    return session::Credentials { .mAccount   = std::string { kAccount },
                                  .mPassword  = std::string { kPassword },
                                  .mLoginName = "mud" };
}

std::unique_ptr<core::Core> makeCore(std::uint16_t                        port,
                                     Collector&                           collector,
                                     std::optional<std::filesystem::path> log,
                                     session::Credentials                 credentials = makeCredentials(),
                                     std::chrono::milliseconds            timeout     = 5s)
{
    core::Builder builder;
    builder.setHost("127.0.0.1")
        .setPort(port)
        .setCredentials(credentials)
        .setColumns(80)
        .setClientVersion("test")
        .setConnectTimeout(timeout)
        .setSink(collector.getSink());
    if (log.has_value())
        builder.setWireLog(*log);
    return builder.build();
}

// Logs in through the fake server and waits for game mode.
void enterGame(core::Core& core, Collector& collector, fake::FakeMudServer& server)
{
    ASSERT_TRUE(core.start());
    // Persona selection is typed by the player, after client mode is entered.
    ASSERT_TRUE(collector.waitFor(isClientMode, 15s)) << server.getError();
    core.submitLine("p");
    core.submitLine(std::string { kPersona });
    ASSERT_TRUE(collector.waitFor(isGameFlow, 15s)) << server.getError();
}

std::string masked(std::string_view line)
{
    return std::string(line.size(), '*') + "\r\n";
}
} // namespace

TEST(CoreIntegration, LogsInEntersGameAndMasksTheWireLog)
{
    const std::filesystem::path         database { makeTempDatabase("login") };
    const std::vector<fake::ScriptStep> script { makeLoginScript() };
    fake::FakeMudServer                 server { script };
    ASSERT_NE(server.getPort(), 0);

    Collector                         collector;
    const std::unique_ptr<core::Core> core { makeCore(server.getPort(), collector, database) };
    enterGame(*core, collector, server);
    ASSERT_TRUE(server.waitDone(15s));
    EXPECT_EQ(server.getError(), "");
    core->stop();

    // Every byte the client sent, exactly and in order: credentials in clear on the wire.
    EXPECT_EQ(server.getReceived(), fake::getExpectedBytes(script));

    // The wire log holds every byte. The credentials and every other pre-game typed line are
    // masked with the same length.
    const std::vector<LogRow> rows { readWireLog(database) };
    ASSERT_FALSE(rows.empty());
    std::vector<std::string> annotations;
    std::string              sent;
    for (const LogRow& row : rows)
    {
        EXPECT_EQ(row.mData.find(kPassword), std::string::npos) << "the password reached the wire log";
        if (row.mDirection == 1)
        {
            EXPECT_EQ(row.mData.find(kAccount), std::string::npos) << "the account ID reached a sent record";
            sent.append(row.mData);
        }
        if (row.mDirection == 2)
            annotations.push_back(row.mData);
    }
    std::string expectedSent { toString(fake::getExpectedBytes(script)) };
    for (const std::string_view line : { kAccount, kPassword, std::string_view { "p" }, kPersona })
    {
        const std::string clear { std::string { line } + "\r\n" };
        expectedSent.replace(expectedSent.find(clear), clear.size(), masked(line));
    }
    EXPECT_EQ(sent, expectedSent);
    EXPECT_NE(std::ranges::find(annotations, "masked: account id"), annotations.end());
    EXPECT_NE(std::ranges::find(annotations, "masked: password"), annotations.end());
    EXPECT_EQ(std::ranges::count(annotations, "masked: pre-game line"), 2);
    EXPECT_NE(std::ranges::find(annotations, "terminal width confirmed: 80"), annotations.end());
}

// Type-ahead: lines typed before the connection exists wait in the FIFO, reach the socket in order,
// and are masked in the wire log though no prompt was live when they were typed.
TEST(CoreIntegration, TypeAheadIsSentInOrderAndMasked)
{
    const std::filesystem::path         database { makeTempDatabase("typeAhead") };
    const std::vector<fake::ScriptStep> script {
        fake::expect("Z7654321\r\n"),
        fake::expect("hunter22\r\n"),
        fake::send("Account ID: "),
        fake::expect("look\r\n"),
    };
    fake::FakeMudServer server { script };
    ASSERT_NE(server.getPort(), 0);

    Collector                         collector;
    const std::unique_ptr<core::Core> core { makeCore(
        server.getPort(), collector, database, session::Credentials {}) };
    core->submitLine("Z7654321");
    core->submitLine("hunter22");
    ASSERT_TRUE(core->start());
    ASSERT_TRUE(collector.waitFor(
        [](const session::SessionEvent& event)
        {
            const auto* mud { std::get_if<mud::Event>(&event) };
            return mud != nullptr && std::holds_alternative<mud::PartialEvent>(mud->mPayload);
        },
        15s))
        << server.getError();
    core->submitLine("look");
    ASSERT_TRUE(server.waitDone(15s));
    EXPECT_EQ(server.getError(), "");
    core->stop();

    std::string sent;
    int         preGameNotes { 0 };
    for (const LogRow& row : readWireLog(database))
    {
        EXPECT_EQ(row.mData.find("Z7654321"), std::string::npos) << "type-ahead reached the wire log";
        EXPECT_EQ(row.mData.find("hunter22"), std::string::npos) << "type-ahead reached the wire log";
        if (row.mDirection == 1)
            sent.append(row.mData);
        if (row.mDirection == 2 && row.mData == "masked: pre-game line")
            ++preGameNotes;
        if (row.mDirection == 2 && row.mData == "masked: account id")
            ++preGameNotes;
    }
    EXPECT_EQ(sent, masked("Z7654321") + masked("hunter22") + masked("look"));
    EXPECT_EQ(preGameNotes, 3);
}

// Invariant #1 at the socket, with several UI-side producers: every line arrives whole, never
// split by another, and each producer's lines arrive in the order it submitted them.
TEST(CoreIntegration, LinesFromSeveralProducersArriveWholeAndInEachProducersOrder)
{
    constexpr int kProducers { 4 };
    constexpr int kLinesEach { 150 };

    std::vector<std::string> expectedLines;
    std::size_t              totalBytes { 0 };
    for (int producer { 0 }; producer < kProducers; ++producer)
    {
        for (int i { 0 }; i < kLinesEach; ++i)
        {
            expectedLines.push_back("say p" + std::to_string(producer) + " n" + std::to_string(i));
            totalBytes += expectedLines.back().size() + 2;
        }
    }

    std::vector<fake::ScriptStep> script { makeLoginScript() };
    const std::size_t             loginBytes { fake::getExpectedBytes(script).size() };
    script.push_back(fake::expectCount(totalBytes));
    fake::FakeMudServer server { script };
    ASSERT_NE(server.getPort(), 0);

    Collector                         collector;
    const std::unique_ptr<core::Core> core { makeCore(server.getPort(), collector, std::nullopt) };
    enterGame(*core, collector, server);

    std::atomic<bool>        isGo {};
    std::vector<std::thread> producers;
    for (int producer { 0 }; producer < kProducers; ++producer)
    {
        producers.emplace_back(
            [&core, &isGo, producer]()
            {
                while (! isGo.load())
                    std::this_thread::yield();
                for (int i { 0 }; i < kLinesEach; ++i)
                    core->submitLine("say p" + std::to_string(producer) + " n" + std::to_string(i));
            });
    }
    isGo.store(true);
    for (std::thread& producer : producers)
        producer.join();

    ASSERT_TRUE(server.waitDone(30s));
    EXPECT_EQ(server.getError(), "");
    core->stop();

    const std::vector<std::byte> received { server.getReceived() };
    ASSERT_GE(received.size(), loginBytes + totalBytes);
    const std::string burst { toString(std::span { received }.subspan(loginBytes, totalBytes)) };

    std::vector<int> nextIndex(kProducers, 0);
    std::size_t      start { 0 };
    int              lines { 0 };
    while (start < burst.size())
    {
        const std::size_t end { burst.find("\r\n", start) };
        ASSERT_NE(end, std::string::npos) << "a line arrived without its CR LF";
        const std::string line { burst.substr(start, end - start) };
        start = end + 2;
        ++lines;
        ASSERT_NE(std::ranges::find(expectedLines, line), expectedLines.end())
            << "a line arrived broken: " << line;
        const int producer { line[5] - '0' };
        const int index { std::stoi(line.substr(line.find(" n") + 2)) };
        EXPECT_EQ(index, nextIndex[static_cast<std::size_t>(producer)])
            << "producer " << producer << " reordered";
        nextIndex[static_cast<std::size_t>(producer)] = index + 1;
    }
    EXPECT_EQ(lines, kProducers * kLinesEach);
}

// Ctrl+C right after Enter: lines already submitted are written before the socket closes.
TEST(CoreIntegration, StopWritesLinesAlreadySubmitted)
{
    std::string tail;
    for (int i { 0 }; i < 50; ++i)
        tail += "say last " + std::to_string(i) + "\r\n";
    const std::vector<fake::ScriptStep> script { fake::send("hold\r\n"), fake::expect(tail) };
    fake::FakeMudServer                 server { script };
    ASSERT_NE(server.getPort(), 0);

    // The core thread is held inside the sink while the lines are submitted and stop() is asked
    // for, so the lines are certainly still in the FIFO when the reactor sees the stop.
    std::mutex              gateMutex;
    std::condition_variable gateChanged;
    bool                    isHeld {};
    bool                    isReleased {};
    Collector               collector;
    const core::EventSink   collect { collector.getSink() };
    core::Builder           builder;
    builder.setHost("127.0.0.1")
        .setPort(server.getPort())
        .setColumns(80)
        .setClientVersion("test")
        .setConnectTimeout(5s)
        .setSink(
            [&](std::vector<session::SessionEvent> batch)
            {
                const bool isHold { std::ranges::any_of(
                    batch,
                    [](const session::SessionEvent& event)
                    {
                        const auto* mud { std::get_if<mud::Event>(&event) };
                        const auto* text { mud != nullptr ? std::get_if<mud::TextEvent>(&mud->mPayload)
                                                          : nullptr };
                        return text != nullptr && text->mText == "hold";
                    }) };
                collect(std::move(batch));
                if (! isHold)
                    return;
                std::unique_lock lock { gateMutex };
                isHeld = true;
                gateChanged.notify_all();
                gateChanged.wait(lock,
                                 [&isReleased]()
                                 {
                                     return isReleased;
                                 });
            });
    const std::unique_ptr<core::Core> core { builder.build() };
    ASSERT_TRUE(core->start());
    {
        std::unique_lock lock { gateMutex };
        ASSERT_TRUE(gateChanged.wait_for(lock,
                                         15s,
                                         [&isHeld]()
                                         {
                                             return isHeld;
                                         }));
    }
    for (int i { 0 }; i < 50; ++i)
        core->submitLine("say last " + std::to_string(i));
    std::thread stopper { [&core]()
                          {
                              core->stop();
                          } };
    std::this_thread::sleep_for(200ms);
    {
        const std::scoped_lock lock { gateMutex };
        isReleased = true;
    }
    gateChanged.notify_all();
    stopper.join();

    ASSERT_TRUE(server.waitDone(15s));
    EXPECT_EQ(server.getError(), "");
}

// Ctrl+C while the connect is still pending returns at once, not at the 15 s timeout.
TEST(CoreIntegration, StopDuringConnectReturnsPromptly)
{
    const transport::Network                        network;
    std::optional<transport::loopback::TcpListener> listener { transport::loopback::TcpListener::listen() };
    ASSERT_TRUE(listener.has_value());
    // Fill the backlog so the core's connect neither completes nor is refused at once.
    std::optional<transport::WakeSocket> wake { transport::WakeSocket::open() };
    ASSERT_TRUE(wake.has_value());
    const std::shared_ptr<const transport::WakeSocket> shared { std::make_shared<const transport::WakeSocket>(
        std::move(*wake)) };
    const std::atomic<bool>                            isCancelled {};
    std::vector<transport::TcpConnection>              fillers;
    for (int i { 0 }; i < 8; ++i)
    {
        transport::ConnectResult filler { transport::connectTcp(
            "127.0.0.1", listener->getPort(), 200ms, shared, isCancelled) };
        if (! filler.mConnection.has_value())
            break;
        fillers.push_back(std::move(*filler.mConnection));
    }
    ASSERT_FALSE(fillers.empty());

    Collector                         collector;
    const std::unique_ptr<core::Core> core { makeCore(
        listener->getPort(), collector, std::nullopt, makeCredentials(), 15s) };
    core->submitLine("never sent");
    ASSERT_TRUE(core->start());
    std::this_thread::sleep_for(300ms);
    // The connect is still pending: it began, and neither connected nor failed.
    bool isConnecting {};
    for (const session::SessionEvent& event : collector.getEvents())
    {
        const auto* connection { std::get_if<session::ConnectionEvent>(&event) };
        if (connection == nullptr)
            continue;
        ASSERT_EQ(connection->mState, session::ConnectionState::kConnecting) << "the connect was not pending";
        isConnecting = true;
    }
    ASSERT_TRUE(isConnecting);
    const auto stoppedAt { std::chrono::steady_clock::now() };
    core->stop();
    EXPECT_LT(std::chrono::steady_clock::now() - stoppedAt, 1s);

    ASSERT_TRUE(collector.waitFor(isClosed, 1s));
    bool isReported {};
    for (const session::SessionEvent& event : collector.getEvents())
    {
        if (const auto* notice { std::get_if<session::NoticeEvent>(&event) }; notice != nullptr)
            isReported = isReported || notice->mText.starts_with("1 typed line was not sent");
        const auto* connection { std::get_if<session::ConnectionEvent>(&event) };
        EXPECT_FALSE(connection != nullptr && connection->mState == session::ConnectionState::kConnected);
    }
    EXPECT_TRUE(isReported);
}

TEST(CoreIntegration, ReportsARefusedConnection)
{
    // A port that was just listening and now is not.
    std::uint16_t port {};
    {
        const transport::Network                              network;
        const std::optional<transport::loopback::TcpListener> listener {
            transport::loopback::TcpListener::listen()
        };
        ASSERT_TRUE(listener.has_value());
        port = listener->getPort();
    }
    Collector                         collector;
    const std::unique_ptr<core::Core> core { makeCore(port, collector, std::nullopt) };
    ASSERT_TRUE(core->start());
    EXPECT_TRUE(collector.waitFor(
        [](const session::SessionEvent& event)
        {
            const auto* connection { std::get_if<session::ConnectionEvent>(&event) };
            return connection != nullptr && connection->mState == session::ConnectionState::kClosed
                   && connection->mReason.starts_with("Cannot connect");
        },
        15s));
    core->stop();
}
