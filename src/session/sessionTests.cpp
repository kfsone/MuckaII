#include "muckaii/session/session.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace muckaii::session;

namespace
{
std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> out;
    for (const char c : text)
        out.push_back(std::byte { static_cast<std::uint8_t>(c) });
    return out;
}

std::vector<std::byte> bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> out;
    for (const int value : values)
        out.push_back(std::byte { static_cast<std::uint8_t>(value) });
    return out;
}

std::vector<Outbound> drain(Session& session)
{
    std::vector<Outbound> sends;
    while (session.hasOutbound())
        sends.push_back(session.takeOutbound());
    return sends;
}

std::vector<FlowState> flowsIn(const std::vector<SessionEvent>& events)
{
    std::vector<FlowState> flows;
    for (const SessionEvent& event : events)
    {
        if (const auto* flow { std::get_if<FlowEvent>(&event) }; flow != nullptr)
            flows.push_back(flow->mState);
    }
    return flows;
}

Session makeSession()
{
    return Session { Credentials { .mAccount = "Z1234567", .mPassword = "secretpw", .mLoginName = "mud" },
                     80 };
}
} // namespace

TEST(Columns, EffectiveColumnsFollowConnectionSection4)
{
    EXPECT_EQ(getEffectiveColumns(120, std::nullopt), 80);
    EXPECT_EQ(getEffectiveColumns(60, std::nullopt), 60);
    EXPECT_EQ(getEffectiveColumns(10, std::nullopt), 20);
    EXPECT_EQ(getEffectiveColumns(200, 150), 150);
    EXPECT_EQ(getEffectiveColumns(200, 300), 160);
}

TEST(ClientMode, EntryIsTheReferenceFormWithTheColumnCount)
{
    EXPECT_EQ(
        makeClientModeEntry(80),
        bytes({ 0x1B, 0x2D, 0x5B, 0x1B, 0x06, 0x1B, 0x2D, 0x4E, 0x2F, 0x54, 0x38, 0x30, 0x1B, 0x2D, 0x5D }));
}

TEST(Session, AnswersTelnetInOrderOnTheOneQueue)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes({ 0xFF, 0xFD, 0x18, 0xFF, 0xFD, 0x1F, 0xFF, 0xFB, 0x03, 0xFF, 0xFB, 0x01 }),
                       events);
    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 3U);
    EXPECT_EQ(sends[0].mBytes, bytes({ 0xFF, 0xFB, 0x18 }));
    EXPECT_EQ(sends[1].mBytes, bytes({ 0xFF, 0xFB, 0x1F, 0xFF, 0xFA, 0x1F, 0x00, 80, 0x00, 21, 0xFF, 0xF0 }));
    EXPECT_EQ(sends[2].mBytes, bytes({ 0xFF, 0xFD, 0x03 }));
    for (const Outbound& send : sends)
        EXPECT_EQ(send.mOrigin, Origin::kTelnet);
}

TEST(Session, LogsInAndEntersClientMode)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes({ 0xFF, 0xFD, 0x1F }), events);
    static_cast<void>(drain(session));

    session.onReceived(bytes("Welcome to MUD2\r\nAccount ID: "), events);
    std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 1U);
    EXPECT_EQ(sends[0].mBytes, bytes("Z1234567\r\n"));
    EXPECT_EQ(sends[0].mMask, Mask::kAccountId);

    // A prompt split across two reads still matches.
    session.onReceived(bytes("Z1234567\r\nPass"), events);
    EXPECT_FALSE(session.hasOutbound());
    session.onReceived(bytes("word: "), events);
    sends = drain(session);
    ASSERT_EQ(sends.size(), 1U);
    EXPECT_EQ(sends[0].mBytes, bytes("secretpw\r\n"));
    EXPECT_EQ(sends[0].mOrigin, Origin::kPassword);
    EXPECT_EQ(sends[0].mMask, Mask::kPassword);

    session.onReceived(bytes("\r\nZ1234567 logged in on tty1.\r\nOption (H for help): "), events);
    sends = drain(session);
    ASSERT_EQ(sends.size(), 2U);
    EXPECT_EQ(sends[0].mBytes, bytes({ 0xFF, 0xFA, 0x1F, 0x00, 80, 0x00, 21, 0xFF, 0xF0 }));
    EXPECT_EQ(
        sends[1].mBytes,
        bytes({ 0x1B, 0x2D, 0x5B, 0x1B, 0x06, 0x1B, 0x2D, 0x4E, '/', 'T', '8', '0', 0x1B, 0x2D, 0x5D }));

    // Once per login: the second Option prompt sends nothing.
    session.onReceived(bytes("\r\nOption (H for help): "), events);
    EXPECT_FALSE(session.hasOutbound());
}

TEST(Session, SendsThePasswordOnlyAfterTheAccount)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes("Password: "), events);
    EXPECT_FALSE(session.hasOutbound());
}

TEST(Session, IgnoresAnOldPasswordLine)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes("Account ID: "), events);
    static_cast<void>(drain(session));
    session.onReceived(bytes("\r\nOld Password: "), events);
    EXPECT_FALSE(session.hasOutbound());
}

TEST(Session, AnswersTheShellLoginOnce)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes("host login: "), events);
    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 1U);
    EXPECT_EQ(sends[0].mBytes, bytes("mud\r\n"));
    session.onReceived(bytes("\r\nhost login: "), events);
    EXPECT_FALSE(session.hasOutbound());
}

TEST(Session, MasksACredentialTypedAtItsPrompt)
{
    Session                   session { Credentials {}, 80 };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes("Account ID: "), events);
    EXPECT_FALSE(session.hasOutbound());
    session.queueLine("Z7654321", false);
    session.onReceived(bytes("Z7654321\r\nPassword: "), events);
    session.queueLine("hunter22", false);
    session.onReceived(bytes("\r\nOption: "), events);
    session.queueLine("p", false);

    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 4U);
    EXPECT_EQ(sends[0].mMask, Mask::kAccountId);
    EXPECT_EQ(getLoggedBytes(sends[0]), bytes("********\r\n"));
    EXPECT_EQ(sends[1].mMask, Mask::kPassword);
    EXPECT_EQ(getLoggedBytes(sends[1]), bytes("********\r\n"));
    EXPECT_EQ(
        sends[2].mBytes,
        bytes({ 0x1B, 0x2D, 0x5B, 0x1B, 0x06, 0x1B, 0x2D, 0x4E, '/', 'T', '8', '0', 0x1B, 0x2D, 0x5D }));
    EXPECT_EQ(sends[2].mMask, Mask::kNone);
    // Login is complete at the Option menu: the pre-game shell is logged verbatim.
    EXPECT_EQ(sends[3].mMask, Mask::kNone);
    EXPECT_EQ(getLoggedBytes(sends[3]), bytes("p\r\n"));
}

// After login only a line answering a recognised password (or account) prompt is masked. A
// password typed at an unrecognised prompt after login ("Old Password:") is logged in clear: the
// trade-off persistence.md's DECISION states.
TEST(Session, AfterLoginOnlyARecognisedCredentialPromptIsMasked)
{
    Session                   session { Credentials {}, 80 };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes("MUD login menu.\r\nOption (H for help): "), events);
    static_cast<void>(drain(session));
    EXPECT_FALSE(session.isLineSensitive());
    session.queueLine("m", false);
    session.onReceived(bytes("m\r\nOld Password: "), events);
    EXPECT_FALSE(session.isInputHidden());
    session.queueLine("oldpass1", false);
    session.onReceived(bytes("\r\nPassword: "), events);
    EXPECT_TRUE(session.isInputHidden());
    EXPECT_TRUE(session.isLineSensitive());
    session.queueLine("hunter22", false);

    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 3U);
    EXPECT_EQ(sends[0].mMask, Mask::kNone);
    EXPECT_EQ(sends[1].mMask, Mask::kNone);
    EXPECT_EQ(getLoggedBytes(sends[1]), bytes("oldpass1\r\n"));
    EXPECT_EQ(sends[2].mMask, Mask::kPassword);
    EXPECT_EQ(getLoggedBytes(sends[2]), bytes("********\r\n"));
}

// Before login the box hides typed text, type-ahead included; after it only a password prompt does.
TEST(Session, InputIsHiddenUntilLoginAndAtAPasswordPrompt)
{
    Session                   session { Credentials {}, 80 };
    std::vector<SessionEvent> events;
    session.onConnecting(events);
    EXPECT_TRUE(session.isInputHidden());
    session.onConnected(events);
    session.onReceived(bytes("Account ID: "), events);
    EXPECT_TRUE(session.isInputHidden());
    session.onReceived(bytes("Z7654321\r\nOption (H for help): "), events);
    EXPECT_FALSE(session.isInputHidden());
}

// A partial after a hidden line (the width notice that answers client-mode entry) replaces the
// live one in the session's view of the line too: a password prompt left live before it does not
// stay recognised.
TEST(Session, APartialAfterAHiddenLineReplacesTheLiveLine)
{
    Session                   session { Credentials {}, 80 };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes("Option (H for help): "), events);
    session.onReceived(bytes("\r\nPassword: "), events);
    EXPECT_TRUE(session.isInputHidden());
    session.onReceived(bytes("[New terminal width is 80]\r\nOption (H for help): "), events);
    EXPECT_FALSE(session.isInputHidden());
}

// Type-ahead: a line typed before any prompt exists (while connecting) is masked in the wire log.
TEST(Session, MasksTypeAheadBeforeAnyPrompt)
{
    Session                   session { Credentials {}, 80 };
    std::vector<SessionEvent> events;
    session.onConnecting(events);
    session.queueLine("Z7654321", false);
    session.onConnected(events);
    session.queueLine("hunter22", false);

    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 2U);
    for (const Outbound& send : sends)
    {
        EXPECT_EQ(send.mMask, Mask::kBeforeLogin);
        EXPECT_EQ(getLoggedBytes(send), bytes("********\r\n"));
        EXPECT_EQ(getMaskNote(send.mMask), "masked: line before login");
    }
    // The socket still gets the line in clear.
    EXPECT_EQ(sends[0].mBytes, bytes("Z7654321\r\n"));
}

// Before login completes, a password typed at a prompt the login drive does not recognise is
// still masked.
TEST(Session, MasksALineTypedAtAnUnrecognisedPrompt)
{
    for (const std::string_view prompt : { "Enter password: ", "PASSWORD: ", "\r\nOld Password: ", "Code? " })
    {
        Session                   session { Credentials {}, 80 };
        std::vector<SessionEvent> events;
        session.onConnected(events);
        session.onReceived(bytes(prompt), events);
        session.queueLine("hunter22", false);
        const std::vector<Outbound> sends { drain(session) };
        ASSERT_EQ(sends.size(), 1U) << prompt;
        EXPECT_NE(sends[0].mMask, Mask::kNone) << prompt;
        EXPECT_EQ(getLoggedBytes(sends[0]), bytes("********\r\n")) << prompt;
        EXPECT_FALSE(getMaskNote(sends[0].mMask).empty()) << prompt;
    }
}

TEST(Session, GameModeLinesAreNotMasked)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    EXPECT_TRUE(session.isLineSensitive());
    session.onReceived(bytes({ 0x9C, 0x9D, 0xFF, 0xFF, 0xFF, 0xFF }), events);
    ASSERT_EQ(session.getFlowState(), FlowState::kGame);
    EXPECT_FALSE(session.isLineSensitive());
    static_cast<void>(drain(session));
    session.queueLine("look", false);
    // A line the frontend was told is sensitive (typed before the game began, drained after) is
    // masked though the flow is Game now.
    session.queueLine("hunter22", true);
    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 2U);
    EXPECT_EQ(sends[0].mMask, Mask::kNone);
    EXPECT_EQ(getLoggedBytes(sends[0]), bytes("look\r\n"));
    EXPECT_NE(sends[1].mMask, Mask::kNone);
    EXPECT_EQ(getLoggedBytes(sends[1]), bytes("********\r\n"));
}

// Every user-originated origin is masked outside game mode; none of the session's own are.
TEST(Session, EveryUserOriginIsMaskedOutsideGameMode)
{
    EXPECT_TRUE(isUserOrigin(Origin::kTyped));
    EXPECT_FALSE(isUserOrigin(Origin::kLogin));
    EXPECT_FALSE(isUserOrigin(Origin::kPassword));
    EXPECT_FALSE(isUserOrigin(Origin::kTelnet));
    EXPECT_FALSE(isUserOrigin(Origin::kWidth));
}

TEST(Session, WipeSecretClearsTheBytes)
{
    std::string secret { "hunter22" };
    wipeSecret(secret);
    EXPECT_TRUE(secret.empty());
    EXPECT_EQ(secret.data()[0], '\0');
}

// A data byte 255 goes on the wire doubled (protocol.md "Telnet residue in the stream").
TEST(Session, MakeLineDoublesAnIacByte)
{
    EXPECT_EQ(makeLine("a\xFF"
                       "b"),
              bytes({ 'a', 0xFF, 0xFF, 'b', '\r', '\n' }));
}

TEST(Session, ACredentialWithAControlByteIsNeverSent)
{
    EXPECT_TRUE(isValidCredential("Z1234567"));
    EXPECT_FALSE(isValidCredential("Z12\r\nlook"));
    EXPECT_FALSE(isValidCredential("pass\xE9"));

    Session session { Credentials { .mAccount = "Z12\r\nlook", .mPassword = "secretpw", .mLoginName = "mud" },
                      80 };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes("Account ID: "), events);
    EXPECT_FALSE(session.hasOutbound());
}

TEST(Session, FlowFollowsGameModeAndTheConnection)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    // A room name (<20 01><02 01>Tea room</>.) enters game mode; the Option menu text leaves it.
    std::vector<std::byte>       room { bytes({ 0xAF, 0x9C, 0xFF, 0xFF, 0x9D, 0x9C, 0xFF, 0xFF }) };
    const std::vector<std::byte> name { bytes("Tea room") };
    const std::vector<std::byte> tail { bytes({ 0xFF, 0xFF, '.', '\r', '\n', 0xFF, 0xFF }) };
    room.insert(room.end(), name.begin(), name.end());
    room.insert(room.end(), tail.begin(), tail.end());
    session.onReceived(room, events);
    EXPECT_EQ(session.getFlowState(), FlowState::kGame);
    session.onReceived(bytes("Cheerio!\r\nOption (H for help): "), events);
    EXPECT_EQ(session.getFlowState(), FlowState::kPregame);
    session.onClosed("closed by test", events);
    EXPECT_EQ(flowsIn(events),
              (std::vector<FlowState> {
                  FlowState::kPregame, FlowState::kGame, FlowState::kPregame, FlowState::kProfile }));
}

TEST(Session, ResizeInGameSendsNawsThenWidth)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes({ 0xFF, 0xFD, 0x1F }), events);
    session.onReceived(bytes({ 0x9C, 0x9D, 0xFF, 0xFF, 0xFF, 0xFF }), events);
    ASSERT_EQ(session.getFlowState(), FlowState::kGame);
    static_cast<void>(drain(session));

    session.setColumns(100);
    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 2U);
    EXPECT_EQ(sends[0].mBytes, bytes({ 0xFF, 0xFA, 0x1F, 0x00, 100, 0x00, 21, 0xFF, 0xF0 }));
    EXPECT_EQ(sends[0].mOrigin, Origin::kTelnet);
    EXPECT_EQ(sends[1].mBytes, bytes({ 0x1B, '-', '[', '/', 'T', '1', '0', '0', 0x1B, '-', ']' }));
    EXPECT_EQ(sends[1].mOrigin, Origin::kWidth);
}
