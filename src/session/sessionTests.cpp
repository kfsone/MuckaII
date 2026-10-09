#include "muckaii/base/types.hpp"
#include "muckaii/base/wire.hpp"
#include "muckaii/session/session.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace muckaii::session;
namespace types     = muckaii::types;
namespace wire      = muckaii::wire;
namespace transport = muckaii::transport;

// kZeroByte is the zero-valued byte in expected protocol frames.
constexpr int kZeroByte { 0x00 };
// kEnvironSendCommand identifies a NEW-ENVIRON SEND request.
constexpr int kEnvironSendCommand { 0x01 };
// kEnvironIsCommand identifies a NEW-ENVIRON IS response.
constexpr int kEnvironIsCommand { kZeroByte };
// kEnvironVariable marks a NEW-ENVIRON variable name.
constexpr int kEnvironVariable { kZeroByte };
// kEnvironValue marks the value following a NEW-ENVIRON variable name.
constexpr int kEnvironValue { 0x01 };
// kClientModeControl is the control byte between the opening and width escapes.
constexpr int kClientModeControl { 0x06 };
// kRoomTitleStart marks the start of the room title in the captured test stream.
constexpr int kRoomTitleStart { 0xAF };
// kGameModeStartCode begins a room display in test wire sequences.
constexpr int kGameModeStartCode { 0x9C };
// kGameModeEndCode closes the room display in test wire sequences.
constexpr int kGameModeEndCode { 0x9D };
// kEnvironUserVariable is the NEW-ENVIRON variable advertised by the session.
constexpr std::string_view kEnvironUserVariable { "USER" };
// kClientWrapLine is the expected typed command that disables server wrapping.
constexpr std::string_view kClientWrapLine { "/F+\r\n" };

namespace
{
// bytes converts test text into the exact byte sequence received by the session.
types::Bytes bytes(std::string_view text)
{
    types::Bytes out;
    for (const char c : text)
        out.push_back(std::byte { static_cast<std::uint8_t>(c) });
    return out;
}

// bytes converts test values into the exact byte sequence received by the session.
types::Bytes bytes(std::initializer_list<int> values)
{
    types::Bytes out;
    for (const int value : values)
        out.push_back(std::byte { static_cast<std::uint8_t>(value) });
    return out;
}

// drain removes every queued send in order for test inspection.
std::vector<Outbound> drain(Session& session)
{
    std::vector<Outbound> sends;
    while (session.hasOutbound())
        sends.push_back(session.takeOutbound());
    return sends;
}

// flowsIn extracts flow changes from session events in their original order.
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

// newEnvironSendRequest builds a SEND request with no requested variable list.
types::Bytes newEnvironSendRequest()
{
    return bytes({ wire::kTelnetIac,
                   wire::kTelnetSb,
                   transport::kOptionNewEnviron,
                   kEnvironSendCommand,
                   wire::kTelnetIac,
                   wire::kTelnetSe });
}

// newEnvironLoginNameResponse builds the response that advertises a login name.
types::Bytes newEnvironLoginNameResponse(std::string_view loginName)
{
    types::Bytes       response { bytes({ wire::kTelnetIac, wire::kTelnetSb }) };
    const types::Bytes header { bytes(
        { transport::kOptionNewEnviron, kEnvironIsCommand, kEnvironVariable }) };
    const types::Bytes variable { bytes(kEnvironUserVariable) };
    response.insert(response.end(), header.begin(), header.end());
    const types::Bytes name { bytes(loginName) };
    response.insert(response.end(), variable.begin(), variable.end());
    response.push_back(std::byte { kEnvironValue });
    response.insert(response.end(), name.begin(), name.end());
    response.push_back(std::byte { wire::kTelnetIac });
    response.push_back(std::byte { wire::kTelnetSe });
    return response;
}

// nawsNegotiationReply builds the expected WILL and NAWS bytes for an 80-column session.
types::Bytes nawsNegotiationReply()
{
    return bytes({ wire::kTelnetIac,
                   wire::kTelnetWill,
                   transport::kOptionNaws,
                   wire::kTelnetIac,
                   wire::kTelnetSb,
                   transport::kOptionNaws,
                   kZeroByte,
                   80,
                   kZeroByte,
                   transport::kNawsRows,
                   wire::kTelnetIac,
                   wire::kTelnetSe });
}

// gameModeSequence returns the bytes that switch the decoder into game mode.
types::Bytes gameModeSequence()
{
    return bytes({ kGameModeStartCode,
                   kGameModeEndCode,
                   wire::kTelnetIac,
                   wire::kTelnetIac,
                   wire::kTelnetIac,
                   wire::kTelnetIac });
}

// roomTitlePrefix returns the opening codes for the captured room-title structure.
types::Bytes roomTitlePrefix()
{
    return bytes({ kRoomTitleStart,
                   kGameModeStartCode,
                   wire::kTelnetIac,
                   wire::kTelnetIac,
                   kGameModeEndCode,
                   kGameModeStartCode,
                   wire::kTelnetIac,
                   wire::kTelnetIac });
}

// roomTitleTail returns the closing bytes that follow the captured room title.
types::Bytes roomTitleTail()
{
    return bytes({ wire::kTelnetIac, wire::kTelnetIac, '.', '\r', '\n', wire::kTelnetIac, wire::kTelnetIac });
}

// telnetNegotiationSequence builds the ordered option requests used by session reply tests.
types::Bytes telnetNegotiationSequence()
{
    return bytes({ wire::kTelnetIac,
                   wire::kTelnetDo,
                   transport::kOptionTtype,
                   wire::kTelnetIac,
                   wire::kTelnetDo,
                   transport::kOptionNaws,
                   wire::kTelnetIac,
                   wire::kTelnetWill,
                   transport::kOptionSga,
                   wire::kTelnetIac,
                   wire::kTelnetWill,
                   transport::kOptionEcho });
}

// makeSession creates a session with valid test credentials and standard width.
Session makeSession()
{
    return Session { Credentials { .mAccount = "Z1234567", .mPassword = "secretpw", .mLoginName = "mud" },
                     80 };
}
} // namespace

// Columns checks the effective width respects the configured and terminal limits.
TEST(Columns, EffectiveColumnsFollowConnectionSection4)
{
    EXPECT_EQ(getEffectiveColumns(120, std::nullopt), 80);
    EXPECT_EQ(getEffectiveColumns(60, std::nullopt), 60);
    EXPECT_EQ(getEffectiveColumns(10, std::nullopt), 20);
    EXPECT_EQ(getEffectiveColumns(200, 150), 150);
    EXPECT_EQ(getEffectiveColumns(200, 300), 160);
}

// 0 is reserved for client-side wrap (connection.md 4): no ceiling at all, not a ceiling of zero.
// Columns checks zero selects client-side wrapping instead of a zero-column ceiling.
TEST(Columns, EffectiveColumnsTreatsZeroAsNoCeilingNotAZeroCeiling)
{
    EXPECT_EQ(getEffectiveColumns(120, 0), 120);
    EXPECT_EQ(getEffectiveColumns(10, 0), 20);   // still never below the floor
    EXPECT_EQ(getEffectiveColumns(200, 0), 160); // still never above the ceiling
}

// ClientMode checks client-mode entry uses the bracketed width form.
TEST(ClientMode, EntryIsTheReferenceFormWithTheColumnCount)
{
    EXPECT_EQ(makeClientModeEntry(80),
              bytes({ wire::kEsc,
                      '-',
                      '[',
                      wire::kEsc,
                      kClientModeControl,
                      wire::kEsc,
                      '-',
                      'N',
                      '/',
                      'T',
                      '8',
                      '0',
                      wire::kEsc,
                      '-',
                      ']' }));
}

// Session checks telnet replies stay ordered on the outbound queue.
TEST(Session, AnswersTelnetInOrderOnTheOneQueue)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(telnetNegotiationSequence(), events);
    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 3U);
    EXPECT_EQ(sends[0].mBytes, bytes({ wire::kTelnetIac, wire::kTelnetWill, transport::kOptionTtype }));
    EXPECT_EQ(sends[1].mBytes, nawsNegotiationReply());
    for (const Outbound& send : sends)
        EXPECT_EQ(send.mOrigin, Origin::kTelnet);
}

// NEW-ENVIRON (connection.md 2.1): a session built with a non-empty, valid login name sends WILL,
// then answers SEND with IS USER=<name>, exactly as the reference does.
// Session checks a valid login name is advertised through NEW-ENVIRON.
TEST(Session, AdvertisesTheLoginNameOverNewEnviron)
{
    Session session { Credentials { .mAccount = "Z1234567", .mPassword = "secretpw", .mLoginName = "mudx" },
                      80 };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes({ wire::kTelnetIac, wire::kTelnetDo, transport::kOptionNewEnviron }), events);
    session.onReceived(newEnvironSendRequest(), events);
    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 2U);
    EXPECT_EQ(sends[0].mBytes, bytes({ wire::kTelnetIac, wire::kTelnetWill, transport::kOptionNewEnviron }));
    EXPECT_EQ(sends[1].mBytes, newEnvironLoginNameResponse("mudx"));
    for (const Outbound& send : sends)
        EXPECT_EQ(send.mOrigin, Origin::kTelnet);
}

// A login name that is empty, or that drivePrompts() would itself refuse to send (not printable
// 7-bit), must not be advertised: NEW-ENVIRON falls back to Clio's WONT, same as an unset name.
// Session checks an unsendable login name is not advertised.
TEST(Session, RefusesNewEnvironWhenTheLoginNameIsNotSendable)
{
    Session session {
        Credentials { .mAccount = "Z1234567", .mPassword = "secretpw", .mLoginName = "bad\r\n" }, 80
    };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes({ wire::kTelnetIac, wire::kTelnetDo, transport::kOptionNewEnviron }), events);
    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 1U);
    EXPECT_EQ(sends[0].mBytes, bytes({ wire::kTelnetIac, wire::kTelnetWont, transport::kOptionNewEnviron }));
}

// Session checks login advances through credentials and enters client mode.
TEST(Session, LogsInAndEntersClientMode)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes({ wire::kTelnetIac, wire::kTelnetDo, transport::kOptionNaws }), events);
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
    EXPECT_EQ(sends[0].mBytes,
              bytes({ wire::kTelnetIac,
                      wire::kTelnetSb,
                      transport::kOptionNaws,
                      kZeroByte,
                      80,
                      kZeroByte,
                      21,
                      wire::kTelnetIac,
                      wire::kTelnetSe }));
    EXPECT_EQ(sends[1].mBytes,
              bytes({ wire::kEsc,
                      '-',
                      '[',
                      wire::kEsc,
                      kClientModeControl,
                      wire::kEsc,
                      '-',
                      'N',
                      '/',
                      'T',
                      '8',
                      '0',
                      wire::kEsc,
                      '-',
                      ']' }));

    // Once per login: the second Option prompt sends nothing.
    session.onReceived(bytes("\r\nOption (H for help): "), events);
    EXPECT_FALSE(session.hasOutbound());
}

// Terminal width 0 ("client-side wrap", connection.md 4, DECISION source: director): client-mode
// entry still carries a real width for ESC-N's grammar, but `/F+` follows it once, telling the
// server to stop wrapping so MuckaII wraps for display instead.
// Session checks client wrapping disables server wrapping after mode entry.
TEST(Session, ClientWrapSendsFPlusAfterClientModeEntry)
{
    Session session { Credentials { .mAccount = "Z1234567", .mPassword = "secretpw", .mLoginName = "mud" },
                      80,
                      /*isClientWrap=*/true };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes("\r\nZ1234567 logged in on tty1.\r\nOption (H for help): "), events);
    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 2U);
    EXPECT_EQ(sends[0].mBytes,
              bytes({ wire::kEsc,
                      '-',
                      '[',
                      wire::kEsc,
                      kClientModeControl,
                      wire::kEsc,
                      '-',
                      'N',
                      '/',
                      'T',
                      '8',
                      '0',
                      wire::kEsc,
                      '-',
                      ']' }));
    EXPECT_EQ(sends[1].mBytes, bytes(kClientWrapLine));
    EXPECT_EQ(sends[1].mOrigin, Origin::kLogin);

    // Once per login, like client-mode entry itself.
    session.onReceived(bytes("\r\nOption (H for help): "), events);
    EXPECT_FALSE(session.hasOutbound());
}

// Without client-wrap, no `/F+` is ever sent (the default, and every other existing test's shape).
// Session checks ordinary width settings never disable server wrapping.
TEST(Session, NoClientWrapNeverSendsFPlus)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes("\r\nZ1234567 logged in on tty1.\r\nOption (H for help): "), events);
    const std::vector<Outbound> sends { drain(session) };
    for (const Outbound& send : sends)
        EXPECT_NE(send.mBytes, bytes(kClientWrapLine));
}

// `/F+` already told the server not to wrap at all, so a later resize in game mode must not
// resend `/T`: that would turn server-side wrapping back on.
// Session checks client wrapping does not re-enable server wrapping after a resize.
TEST(Session, ClientWrapNeverResendsWidthOnResize)
{
    Session session { Credentials { .mAccount = "Z1234567", .mPassword = "secretpw", .mLoginName = "mud" },
                      80,
                      /*isClientWrap=*/true };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes({ wire::kTelnetIac, wire::kTelnetDo, transport::kOptionNaws }), events);
    session.onReceived(gameModeSequence(), events);
    ASSERT_EQ(session.getFlowState(), FlowState::kGame);
    static_cast<void>(drain(session));

    session.setColumns(100);
    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 1U);
    EXPECT_EQ(sends[0].mBytes,
              bytes({ wire::kTelnetIac,
                      wire::kTelnetSb,
                      transport::kOptionNaws,
                      kZeroByte,
                      100,
                      kZeroByte,
                      21,
                      wire::kTelnetIac,
                      wire::kTelnetSe }));
    EXPECT_EQ(sends[0].mOrigin, Origin::kTelnet);
}

// Session checks the password follows the account ID response.
TEST(Session, SendsThePasswordOnlyAfterTheAccount)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes("Password: "), events);
    EXPECT_FALSE(session.hasOutbound());
}

// Session checks an old password line is not treated as a current credential prompt.
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

// Session checks the shell login prompt is answered only once.
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

// Session checks typed credentials are masked at recognized prompts.
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
    EXPECT_EQ(sends[2].mBytes,
              bytes({ wire::kEsc,
                      '-',
                      '[',
                      wire::kEsc,
                      kClientModeControl,
                      wire::kEsc,
                      '-',
                      'N',
                      '/',
                      'T',
                      '8',
                      '0',
                      wire::kEsc,
                      '-',
                      ']' }));
    EXPECT_EQ(sends[2].mMask, Mask::kNone);
    // Login is complete at the Option menu: the pre-game shell is logged verbatim.
    EXPECT_EQ(sends[3].mMask, Mask::kNone);
    EXPECT_EQ(getLoggedBytes(sends[3]), bytes("p\r\n"));
}

// After login only a line answering a recognised password (or account) prompt is masked. A
// password typed at an unrecognised prompt after login ("Old Password:") is logged in clear: the
// trade-off persistence.md's DECISION states.
// Session checks only recognized credential prompts are masked after login.
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
    EXPECT_FALSE(session.isLineSensitive());
    session.queueLine("oldpass1", false);
    session.onReceived(bytes("\r\nPassword: "), events);
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

// Every line is masked until login completes, type-ahead included.
// Session checks typed lines remain sensitive until login completes.
TEST(Session, LinesAreSensitiveUntilLogin)
{
    Session                   session { Credentials {}, 80 };
    std::vector<SessionEvent> events;
    session.onConnecting(events);
    EXPECT_TRUE(session.isLineSensitive());
    session.onConnected(events);
    session.onReceived(bytes("Account ID: "), events);
    EXPECT_TRUE(session.isLineSensitive());
    session.onReceived(bytes("Z7654321\r\nOption (H for help): "), events);
    EXPECT_FALSE(session.isLineSensitive());
}

// A partial after a hidden line (the width notice that answers client-mode entry) replaces the
// live one in the session's view of the line too: a password prompt left live before it does not
// stay recognised.
// Session checks a partial after a hidden line replaces the previous live prompt.
TEST(Session, APartialAfterAHiddenLineReplacesTheLiveLine)
{
    Session                   session { Credentials {}, 80 };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes("Option (H for help): "), events);
    session.onReceived(bytes("\r\nPassword: "), events);
    EXPECT_TRUE(session.isLineSensitive());
    session.onReceived(bytes("[New terminal width is 80]\r\nOption (H for help): "), events);
    EXPECT_FALSE(session.isLineSensitive());
}

// Type-ahead: a line typed before any prompt exists (while connecting) is masked in the wire log.
// Session checks type-ahead is masked before a prompt appears.
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
// Session checks unrecognized prompts still mask lines before login completes.
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

// Session checks game-mode lines are not masked.
TEST(Session, GameModeLinesAreNotMasked)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    EXPECT_TRUE(session.isLineSensitive());
    session.onReceived(gameModeSequence(), events);
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
// Session checks only user-originated sends are masked outside game mode.
TEST(Session, EveryUserOriginIsMaskedOutsideGameMode)
{
    EXPECT_TRUE(isUserOrigin(Origin::kTyped));
    EXPECT_FALSE(isUserOrigin(Origin::kLogin));
    EXPECT_FALSE(isUserOrigin(Origin::kPassword));
    EXPECT_FALSE(isUserOrigin(Origin::kTelnet));
    EXPECT_FALSE(isUserOrigin(Origin::kWidth));
}

// Session checks wiping a secret clears its contents.
TEST(Session, WipeSecretClearsTheBytes)
{
    std::string secret { "hunter22" };
    wipeSecret(secret);
    EXPECT_TRUE(secret.empty());
    EXPECT_EQ(secret.data()[0], '\0');
}

// A data byte 255 goes on the wire doubled (protocol.md "Telnet residue in the stream").
// Session checks line construction doubles a data IAC byte.
TEST(Session, MakeLineDoublesAnIacByte)
{
    EXPECT_EQ(makeLine("a\xFF"
                       "b"),
              bytes({ 'a', wire::kTelnetIac, wire::kTelnetIac, 'b', '\r', '\n' }));
}

// Session checks credentials containing control bytes are never sent.
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

// Session checks flow state follows game markers and connection closure.
TEST(Session, FlowFollowsGameModeAndTheConnection)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    // A room name (<20 01><02 01>Tea room</>.) enters game mode; the Option menu text leaves it.
    types::Bytes       room { roomTitlePrefix() };
    const types::Bytes name { bytes("Tea room") };
    const types::Bytes tail { roomTitleTail() };
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

// Session checks an in-game resize sends NAWS before the width command.
TEST(Session, ResizeInGameSendsNawsThenWidth)
{
    Session                   session { makeSession() };
    std::vector<SessionEvent> events;
    session.onConnected(events);
    session.onReceived(bytes({ wire::kTelnetIac, wire::kTelnetDo, transport::kOptionNaws }), events);
    session.onReceived(gameModeSequence(), events);
    ASSERT_EQ(session.getFlowState(), FlowState::kGame);
    static_cast<void>(drain(session));

    session.setColumns(100);
    const std::vector<Outbound> sends { drain(session) };
    ASSERT_EQ(sends.size(), 2U);
    EXPECT_EQ(sends[0].mBytes,
              bytes({ wire::kTelnetIac,
                      wire::kTelnetSb,
                      transport::kOptionNaws,
                      kZeroByte,
                      100,
                      kZeroByte,
                      21,
                      wire::kTelnetIac,
                      wire::kTelnetSe }));
    EXPECT_EQ(sends[0].mOrigin, Origin::kTelnet);
    EXPECT_EQ(sends[1].mBytes,
              bytes({ wire::kEsc, '-', '[', '/', 'T', '1', '0', '0', wire::kEsc, '-', ']' }));
    EXPECT_EQ(sends[1].mOrigin, Origin::kWidth);
}
